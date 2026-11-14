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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; uint8_t f1; uint8_t pe[7]; } gm_th2bqc58ss467;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; gm_t3mf58gv9kf3n f3; } gm_t5oniao0al09q;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_t4ce0jd3tdro4 f0; uint8_t f1; uint8_t pe[7]; } gm_t6ie8iajlnpt1;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; } gm_t6ofeqo2h242p;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; } gm_t724v60om7sh5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; } gm_t9rbkbpjtolar;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_th2bqc58ss467) == 16 && _Alignof(gm_th2bqc58ss467) == 8 && offsetof(gm_th2bqc58ss467, f0) == 0 && offsetof(gm_th2bqc58ss467, f1) == 8, "layout of `?f64`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_tsoiri44d0i6q f0; uint8_t f1; uint8_t pe[3]; } gm_tqnlpelep1582;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; uint16_t v1; } f1; } gm_tvcqs9u9iqpct;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_th2lqbo77pvsc f1; uint64_t f2; } gm_t04fuf8kbk07t;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t5oniao0al09q) == 48 && _Alignof(gm_t5oniao0al09q) == 8 && offsetof(gm_t5oniao0al09q, f0) == 0 && offsetof(gm_t5oniao0al09q, f1) == 16 && offsetof(gm_t5oniao0al09q, f2) == 24 && offsetof(gm_t5oniao0al09q, f3) == 32, "layout of `Deque(cont(()))`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6ie8iajlnpt1) == 16 && _Alignof(gm_t6ie8iajlnpt1) == 8 && offsetof(gm_t6ie8iajlnpt1, f0) == 0 && offsetof(gm_t6ie8iajlnpt1, f1) == 8, "layout of `?Duration`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6ofeqo2h242p) == 7 && _Alignof(gm_t6ofeqo2h242p) == 1 && offsetof(gm_t6ofeqo2h242p, f0) == 0 && offsetof(gm_t6ofeqo2h242p, f1) == 1 && offsetof(gm_t6ofeqo2h242p, f2) == 2 && offsetof(gm_t6ofeqo2h242p, f3) == 3 && offsetof(gm_t6ofeqo2h242p, f4) == 4 && offsetof(gm_t6ofeqo2h242p, f5) == 5 && offsetof(gm_t6ofeqo2h242p, f6) == 6, "layout of `(Command, Command, Command, Command, Command, Command, Command)`");
_Static_assert(sizeof(gm_t724v60om7sh5) == 5 && _Alignof(gm_t724v60om7sh5) == 1 && offsetof(gm_t724v60om7sh5, f0) == 0 && offsetof(gm_t724v60om7sh5, f1) == 1 && offsetof(gm_t724v60om7sh5, f2) == 2 && offsetof(gm_t724v60om7sh5, f3) == 3 && offsetof(gm_t724v60om7sh5, f4) == 4, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_t9rbkbpjtolar) == 6 && _Alignof(gm_t9rbkbpjtolar) == 1 && offsetof(gm_t9rbkbpjtolar, f0) == 0 && offsetof(gm_t9rbkbpjtolar, f1) == 1 && offsetof(gm_t9rbkbpjtolar, f2) == 2 && offsetof(gm_t9rbkbpjtolar, f3) == 3 && offsetof(gm_t9rbkbpjtolar, f4) == 4 && offsetof(gm_t9rbkbpjtolar, f5) == 5, "layout of `(Constraint, Constraint, Constraint, Constraint, Constraint, Constraint)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tj5jn79kq9pnm v0; uint8_t v1; } f1; } gm_t9t0nevf2k2cl;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
typedef struct { gm_tfeab279c16rr f0; uint8_t f1; uint8_t pe[7]; } gm_td6svc7gs468a;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint8_t f3; gm_t9qkh0rsooc8d f4; gm_tqs5p4n2tq19v f5; uint8_t p6[4]; gm_th2lqbo77pvsc f6; } gm_tiaut12ia9v85;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[5]; gm_trqfvmjtt4sij f3; } gm_tm4vhr17lpr3p;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; } gm_tpcj7kul6hlib;
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tqnlpelep1582) == 12 && _Alignof(gm_tqnlpelep1582) == 4 && offsetof(gm_tqnlpelep1582, f0) == 0 && offsetof(gm_tqnlpelep1582, f1) == 8, "layout of `?ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tqnlpelep1582 v1; } f1; } gm_tr6norheal11n;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; double v1; } f1; } gm_tspt3vn9ljcnd;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; } gm_tvcg39brotose;
_Static_assert(sizeof(gm_tvcqs9u9iqpct) == 12 && _Alignof(gm_tvcqs9u9iqpct) == 4 && offsetof(gm_tvcqs9u9iqpct, f0) == 0 && offsetof(gm_tvcqs9u9iqpct, f1) == 4, "layout of `IoError!u16`");
typedef struct { gm_trqfvmjtt4sij f0; void * f1; } gm_tvtvad16a0doj;
_Static_assert(sizeof(gm_t04fuf8kbk07t) == 40 && _Alignof(gm_t04fuf8kbk07t) == 8 && offsetof(gm_t04fuf8kbk07t, f0) == 0 && offsetof(gm_t04fuf8kbk07t, f1) == 8 && offsetof(gm_t04fuf8kbk07t, f2) == 32, "layout of `Hit`");
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t9t0nevf2k2cl) == 16 && _Alignof(gm_t9t0nevf2k2cl) == 4 && offsetof(gm_t9t0nevf2k2cl, f0) == 0 && offsetof(gm_t9t0nevf2k2cl, f1) == 4, "layout of `ReadError!u8`");
typedef struct { void * f0; gm_tpcj7kul6hlib f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint8_t f9; uint8_t p10[7]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; uint8_t f12; uint8_t p13[7]; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; gm_trqfvmjtt4sij f15; gm_trqfvmjtt4sij f16; gm_trqfvmjtt4sij f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_t9805qtonbsag f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; gm_trqfvmjtt4sij f24; gm_trqfvmjtt4sij f25; gm_trqfvmjtt4sij f26; gm_t4c7ipjj9s3fp f27; gm_t4c7ipjj9s3fp f28; gm_trqfvmjtt4sij f29; uint32_t f30; uint8_t f31; uint8_t p32[3]; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; gm_trqfvmjtt4sij f38; gm_trqfvmjtt4sij f39; gm_trqfvmjtt4sij f40; gm_trqfvmjtt4sij f41; uint32_t f42; uint8_t p43[4]; gm_trqfvmjtt4sij f43; uint32_t f44; uint8_t p45[4]; gm_trqfvmjtt4sij f45; uint8_t f46; uint8_t p47[7]; gm_trqfvmjtt4sij f47; gm_trqfvmjtt4sij f48; uint32_t f49; gm_t9805qtonbsag f50; uint8_t p51[4]; gm_trqfvmjtt4sij f51; gm_t4c7ipjj9s3fp f52; gm_t4c7ipjj9s3fp f53; gm_trqfvmjtt4sij f54; uint32_t f55; uint8_t p56[4]; void * f56; void * f57; void * f58; gm_trqfvmjtt4sij f59; gm_trqfvmjtt4sij f60; gm_trqfvmjtt4sij f61; uint64_t f62; gm_t4c7ipjj9s3fp f63; gm_trqfvmjtt4sij f64; gm_trqfvmjtt4sij f65; gm_trqfvmjtt4sij f66; gm_trqfvmjtt4sij f67; uint8_t f68; uint8_t p69[7]; gm_trqfvmjtt4sij f69; gm_trqfvmjtt4sij f70; gm_t4c7ipjj9s3fp f71; gm_t4c7ipjj9s3fp f72; gm_t4c7ipjj9s3fp f73; } gm_tcrr6nmqqhjbj;
typedef struct { gm_tr8dvlu9ik4gj f0; uint8_t f1; uint8_t pe[7]; } gm_tctputthhrk9k;
_Static_assert(sizeof(gm_td6svc7gs468a) == 56 && _Alignof(gm_td6svc7gs468a) == 8 && offsetof(gm_td6svc7gs468a, f0) == 0 && offsetof(gm_td6svc7gs468a, f1) == 48, "layout of `?Toolchain`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tvcg39brotose v0; gm_b16_8 v1; } f1; } gm_tdpp5715iet0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t04fuf8kbk07t v0; gm_b16_8 v1; } f1; } gm_tepv5jklv667k;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
typedef struct { gm_t5oniao0al09q f0; gm_tvtvad16a0doj f1; uint64_t f2; uint64_t f3; gm_tc70a9dbbmgkg f4; gm_th2lqbo77pvsc f5; uint64_t f6; } gm_th881cfrl0kco;
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tiaut12ia9v85) == 80 && _Alignof(gm_tiaut12ia9v85) == 8 && offsetof(gm_tiaut12ia9v85, f0) == 0 && offsetof(gm_tiaut12ia9v85, f1) == 16 && offsetof(gm_tiaut12ia9v85, f2) == 32 && offsetof(gm_tiaut12ia9v85, f3) == 48 && offsetof(gm_tiaut12ia9v85, f4) == 49 && offsetof(gm_tiaut12ia9v85, f5) == 50 && offsetof(gm_tiaut12ia9v85, f6) == 56, "layout of `Setup`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tm4vhr17lpr3p) == 48 && _Alignof(gm_tm4vhr17lpr3p) == 8 && offsetof(gm_tm4vhr17lpr3p, f0) == 0 && offsetof(gm_tm4vhr17lpr3p, f1) == 1 && offsetof(gm_tm4vhr17lpr3p, f2) == 2 && offsetof(gm_tm4vhr17lpr3p, f3) == 8, "layout of `Options`");
typedef struct { gm_ti0g3bmheuq9o f0; uint8_t f1; uint8_t pe[7]; } gm_tmktnt0k5ner6;
_Static_assert(sizeof(gm_tpcj7kul6hlib) == 296 && _Alignof(gm_tpcj7kul6hlib) == 8 && offsetof(gm_tpcj7kul6hlib, f0) == 0 && offsetof(gm_tpcj7kul6hlib, f1) == 40 && offsetof(gm_tpcj7kul6hlib, f2) == 80 && offsetof(gm_tpcj7kul6hlib, f3) == 136 && offsetof(gm_tpcj7kul6hlib, f4) == 176 && offsetof(gm_tpcj7kul6hlib, f5) == 216 && offsetof(gm_tpcj7kul6hlib, f6) == 256, "layout of `Types`");
_Static_assert(sizeof(gm_tr6norheal11n) == 16 && _Alignof(gm_tr6norheal11n) == 4 && offsetof(gm_tr6norheal11n, f0) == 0 && offsetof(gm_tr6norheal11n, f1) == 4, "layout of `IoError!?ExitStatus`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_tspt3vn9ljcnd) == 24 && _Alignof(gm_tspt3vn9ljcnd) == 8 && offsetof(gm_tspt3vn9ljcnd, f0) == 0 && offsetof(gm_tspt3vn9ljcnd, f1) == 8, "layout of `ParseError!f64`");
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
_Static_assert(sizeof(gm_tvcg39brotose) == 48 && _Alignof(gm_tvcg39brotose) == 8 && offsetof(gm_tvcg39brotose, f0) == 0 && offsetof(gm_tvcg39brotose, f1) == 1 && offsetof(gm_tvcg39brotose, f2) == 2 && offsetof(gm_tvcg39brotose, f3) == 8 && offsetof(gm_tvcg39brotose, f4) == 32, "layout of `Options`");
_Static_assert(sizeof(gm_tvtvad16a0doj) == 48 && _Alignof(gm_tvtvad16a0doj) == 8 && offsetof(gm_tvtvad16a0doj, f0) == 0 && offsetof(gm_tvtvad16a0doj, f1) == 40, "layout of `Heap(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tmktnt0k5ner6 v1; } f1; } gm_t9h00sl1nspi5;
_Static_assert(sizeof(gm_tcrr6nmqqhjbj) == 2792 && _Alignof(gm_tcrr6nmqqhjbj) == 8 && offsetof(gm_tcrr6nmqqhjbj, f0) == 0 && offsetof(gm_tcrr6nmqqhjbj, f1) == 8 && offsetof(gm_tcrr6nmqqhjbj, f2) == 304 && offsetof(gm_tcrr6nmqqhjbj, f3) == 344 && offsetof(gm_tcrr6nmqqhjbj, f4) == 384 && offsetof(gm_tcrr6nmqqhjbj, f5) == 424 && offsetof(gm_tcrr6nmqqhjbj, f6) == 464 && offsetof(gm_tcrr6nmqqhjbj, f7) == 504 && offsetof(gm_tcrr6nmqqhjbj, f8) == 544 && offsetof(gm_tcrr6nmqqhjbj, f9) == 584 && offsetof(gm_tcrr6nmqqhjbj, f10) == 592 && offsetof(gm_tcrr6nmqqhjbj, f11) == 632 && offsetof(gm_tcrr6nmqqhjbj, f12) == 672 && offsetof(gm_tcrr6nmqqhjbj, f13) == 680 && offsetof(gm_tcrr6nmqqhjbj, f14) == 720 && offsetof(gm_tcrr6nmqqhjbj, f15) == 760 && offsetof(gm_tcrr6nmqqhjbj, f16) == 800 && offsetof(gm_tcrr6nmqqhjbj, f17) == 840 && offsetof(gm_tcrr6nmqqhjbj, f18) == 880 && offsetof(gm_tcrr6nmqqhjbj, f19) == 920 && offsetof(gm_tcrr6nmqqhjbj, f20) == 960 && offsetof(gm_tcrr6nmqqhjbj, f21) == 968 && offsetof(gm_tcrr6nmqqhjbj, f22) == 1008 && offsetof(gm_tcrr6nmqqhjbj, f23) == 1048 && offsetof(gm_tcrr6nmqqhjbj, f24) == 1088 && offsetof(gm_tcrr6nmqqhjbj, f25) == 1128 && offsetof(gm_tcrr6nmqqhjbj, f26) == 1168 && offsetof(gm_tcrr6nmqqhjbj, f27) == 1208 && offsetof(gm_tcrr6nmqqhjbj, f28) == 1264 && offsetof(gm_tcrr6nmqqhjbj, f29) == 1320 && offsetof(gm_tcrr6nmqqhjbj, f30) == 1360 && offsetof(gm_tcrr6nmqqhjbj, f31) == 1364 && offsetof(gm_tcrr6nmqqhjbj, f32) == 1368 && offsetof(gm_tcrr6nmqqhjbj, f33) == 1408 && offsetof(gm_tcrr6nmqqhjbj, f34) == 1448 && offsetof(gm_tcrr6nmqqhjbj, f35) == 1488 && offsetof(gm_tcrr6nmqqhjbj, f36) == 1528 && offsetof(gm_tcrr6nmqqhjbj, f37) == 1568 && offsetof(gm_tcrr6nmqqhjbj, f38) == 1608 && offsetof(gm_tcrr6nmqqhjbj, f39) == 1648 && offsetof(gm_tcrr6nmqqhjbj, f40) == 1688 && offsetof(gm_tcrr6nmqqhjbj, f41) == 1728 && offsetof(gm_tcrr6nmqqhjbj, f42) == 1768 && offsetof(gm_tcrr6nmqqhjbj, f43) == 1776 && offsetof(gm_tcrr6nmqqhjbj, f44) == 1816 && offsetof(gm_tcrr6nmqqhjbj, f45) == 1824 && offsetof(gm_tcrr6nmqqhjbj, f46) == 1864 && offsetof(gm_tcrr6nmqqhjbj, f47) == 1872 && offsetof(gm_tcrr6nmqqhjbj, f48) == 1912 && offsetof(gm_tcrr6nmqqhjbj, f49) == 1952 && offsetof(gm_tcrr6nmqqhjbj, f50) == 1956 && offsetof(gm_tcrr6nmqqhjbj, f51) == 1968 && offsetof(gm_tcrr6nmqqhjbj, f52) == 2008 && offsetof(gm_tcrr6nmqqhjbj, f53) == 2064 && offsetof(gm_tcrr6nmqqhjbj, f54) == 2120 && offsetof(gm_tcrr6nmqqhjbj, f55) == 2160 && offsetof(gm_tcrr6nmqqhjbj, f56) == 2168 && offsetof(gm_tcrr6nmqqhjbj, f57) == 2176 && offsetof(gm_tcrr6nmqqhjbj, f58) == 2184 && offsetof(gm_tcrr6nmqqhjbj, f59) == 2192 && offsetof(gm_tcrr6nmqqhjbj, f60) == 2232 && offsetof(gm_tcrr6nmqqhjbj, f61) == 2272 && offsetof(gm_tcrr6nmqqhjbj, f62) == 2312 && offsetof(gm_tcrr6nmqqhjbj, f63) == 2320 && offsetof(gm_tcrr6nmqqhjbj, f64) == 2376 && offsetof(gm_tcrr6nmqqhjbj, f65) == 2416 && offsetof(gm_tcrr6nmqqhjbj, f66) == 2456 && offsetof(gm_tcrr6nmqqhjbj, f67) == 2496 && offsetof(gm_tcrr6nmqqhjbj, f68) == 2536 && offsetof(gm_tcrr6nmqqhjbj, f69) == 2544 && offsetof(gm_tcrr6nmqqhjbj, f70) == 2584 && offsetof(gm_tcrr6nmqqhjbj, f71) == 2624 && offsetof(gm_tcrr6nmqqhjbj, f72) == 2680 && offsetof(gm_tcrr6nmqqhjbj, f73) == 2736, "layout of `Sema`");
_Static_assert(sizeof(gm_tctputthhrk9k) == 128 && _Alignof(gm_tctputthhrk9k) == 8 && offsetof(gm_tctputthhrk9k, f0) == 0 && offsetof(gm_tctputthhrk9k, f1) == 120, "layout of `?Extras`");
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t f4; uint8_t p5[6]; gm_trqfvmjtt4sij f5; gm_b16_8 f6; gm_t0rfe1ou14h21 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_trqfvmjtt4sij f13; gm_tf5srh4mp4b3i f14; gm_trqfvmjtt4sij f15; gm_tv37d3dpt2opu f16; void * f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_trqfvmjtt4sij f20; } gm_tcuh1vih4g9cr;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[3]; gm_th2lqbo77pvsc f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t p9[5]; gm_b16_8 f9; uint8_t f10; uint8_t f11; uint8_t p12[6]; gm_th2lqbo77pvsc f12; uint8_t f13; uint8_t f14; uint8_t p15[6]; uint64_t f15; gm_th2bqc58ss467 f16; uint8_t f17; uint8_t f18; uint8_t p19[6]; gm_th2lqbo77pvsc f19; gm_th2lqbo77pvsc f20; uint8_t f21; uint8_t f22; uint8_t p23[6]; gm_th2lqbo77pvsc f23; uint8_t f24; uint8_t p25[7]; gm_b16_8 f25; gm_b16_8 f26; gm_tctputthhrk9k f27; gm_th2lqbo77pvsc f28; gm_t0rfe1ou14h21 f29; gm_td6svc7gs468a f30; gm_th2lqbo77pvsc f31; gm_tm4vhr17lpr3p f32; } gm_tdkfkbgm73fql;
_Static_assert(sizeof(gm_tdpp5715iet0a) == 56 && _Alignof(gm_tdpp5715iet0a) == 8 && offsetof(gm_tdpp5715iet0a, f0) == 0 && offsetof(gm_tdpp5715iet0a, f1) == 8, "layout of `Cli`");
_Static_assert(sizeof(gm_tepv5jklv667k) == 48 && _Alignof(gm_tepv5jklv667k) == 8 && offsetof(gm_tepv5jklv667k, f0) == 0 && offsetof(gm_tepv5jklv667k, f1) == 8, "layout of `Scan`");
_Static_assert(sizeof(gm_th881cfrl0kco) == 168 && _Alignof(gm_th881cfrl0kco) == 8 && offsetof(gm_th881cfrl0kco, f0) == 0 && offsetof(gm_th881cfrl0kco, f1) == 48 && offsetof(gm_th881cfrl0kco, f2) == 96 && offsetof(gm_th881cfrl0kco, f3) == 104 && offsetof(gm_th881cfrl0kco, f4) == 112 && offsetof(gm_th881cfrl0kco, f5) == 136 && offsetof(gm_th881cfrl0kco, f6) == 160, "layout of `Scheduler`");
typedef struct { gm_tv37d3dpt2opu f0; gm_tr8dvlu9ik4gj f1; } gm_tljis7hgku5bj;
_Static_assert(sizeof(gm_tmktnt0k5ner6) == 160 && _Alignof(gm_tmktnt0k5ner6) == 8 && offsetof(gm_tmktnt0k5ner6, f0) == 0 && offsetof(gm_tmktnt0k5ner6, f1) == 152, "layout of `?BuildSettings`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_t9h00sl1nspi5) == 168 && _Alignof(gm_t9h00sl1nspi5) == 8 && offsetof(gm_t9h00sl1nspi5, f0) == 0 && offsetof(gm_t9h00sl1nspi5, f1) == 8, "layout of `string!?BuildSettings`");
_Static_assert(sizeof(gm_tcuh1vih4g9cr) == 552 && _Alignof(gm_tcuh1vih4g9cr) == 8 && offsetof(gm_tcuh1vih4g9cr, f0) == 0 && offsetof(gm_tcuh1vih4g9cr, f1) == 8 && offsetof(gm_tcuh1vih4g9cr, f2) == 16 && offsetof(gm_tcuh1vih4g9cr, f3) == 24 && offsetof(gm_tcuh1vih4g9cr, f4) == 25 && offsetof(gm_tcuh1vih4g9cr, f5) == 32 && offsetof(gm_tcuh1vih4g9cr, f6) == 72 && offsetof(gm_tcuh1vih4g9cr, f7) == 88 && offsetof(gm_tcuh1vih4g9cr, f8) == 152 && offsetof(gm_tcuh1vih4g9cr, f9) == 168 && offsetof(gm_tcuh1vih4g9cr, f10) == 184 && offsetof(gm_tcuh1vih4g9cr, f11) == 200 && offsetof(gm_tcuh1vih4g9cr, f12) == 240 && offsetof(gm_tcuh1vih4g9cr, f13) == 280 && offsetof(gm_tcuh1vih4g9cr, f14) == 320 && offsetof(gm_tcuh1vih4g9cr, f15) == 336 && offsetof(gm_tcuh1vih4g9cr, f16) == 376 && offsetof(gm_tcuh1vih4g9cr, f17) == 424 && offsetof(gm_tcuh1vih4g9cr, f18) == 432 && offsetof(gm_tcuh1vih4g9cr, f19) == 472 && offsetof(gm_tcuh1vih4g9cr, f20) == 512, "layout of `Loader`");
_Static_assert(sizeof(gm_tdkfkbgm73fql) == 592 && _Alignof(gm_tdkfkbgm73fql) == 8 && offsetof(gm_tdkfkbgm73fql, f0) == 0 && offsetof(gm_tdkfkbgm73fql, f1) == 1 && offsetof(gm_tdkfkbgm73fql, f2) == 2 && offsetof(gm_tdkfkbgm73fql, f3) == 3 && offsetof(gm_tdkfkbgm73fql, f4) == 4 && offsetof(gm_tdkfkbgm73fql, f5) == 8 && offsetof(gm_tdkfkbgm73fql, f6) == 32 && offsetof(gm_tdkfkbgm73fql, f7) == 33 && offsetof(gm_tdkfkbgm73fql, f8) == 34 && offsetof(gm_tdkfkbgm73fql, f9) == 40 && offsetof(gm_tdkfkbgm73fql, f10) == 56 && offsetof(gm_tdkfkbgm73fql, f11) == 57 && offsetof(gm_tdkfkbgm73fql, f12) == 64 && offsetof(gm_tdkfkbgm73fql, f13) == 88 && offsetof(gm_tdkfkbgm73fql, f14) == 89 && offsetof(gm_tdkfkbgm73fql, f15) == 96 && offsetof(gm_tdkfkbgm73fql, f16) == 104 && offsetof(gm_tdkfkbgm73fql, f17) == 120 && offsetof(gm_tdkfkbgm73fql, f18) == 121 && offsetof(gm_tdkfkbgm73fql, f19) == 128 && offsetof(gm_tdkfkbgm73fql, f20) == 152 && offsetof(gm_tdkfkbgm73fql, f21) == 176 && offsetof(gm_tdkfkbgm73fql, f22) == 177 && offsetof(gm_tdkfkbgm73fql, f23) == 184 && offsetof(gm_tdkfkbgm73fql, f24) == 208 && offsetof(gm_tdkfkbgm73fql, f25) == 216 && offsetof(gm_tdkfkbgm73fql, f26) == 232 && offsetof(gm_tdkfkbgm73fql, f27) == 248 && offsetof(gm_tdkfkbgm73fql, f28) == 376 && offsetof(gm_tdkfkbgm73fql, f29) == 400 && offsetof(gm_tdkfkbgm73fql, f30) == 464 && offsetof(gm_tdkfkbgm73fql, f31) == 520 && offsetof(gm_tdkfkbgm73fql, f32) == 544, "layout of `Options`");
typedef struct { gm_tljis7hgku5bj f0; uint8_t f1; uint8_t pe[7]; } gm_tdsi8a7qahakk;
_Static_assert(sizeof(gm_tljis7hgku5bj) == 168 && _Alignof(gm_tljis7hgku5bj) == 8 && offsetof(gm_tljis7hgku5bj, f0) == 0 && offsetof(gm_tljis7hgku5bj, f1) == 48, "layout of `Prepared`");
_Static_assert(sizeof(gm_tdsi8a7qahakk) == 176 && _Alignof(gm_tdsi8a7qahakk) == 8 && offsetof(gm_tdsi8a7qahakk, f0) == 0 && offsetof(gm_tdsi8a7qahakk, f1) == 168, "layout of `?Prepared`");
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_apply_build_settings_bc3dtmk {
    gm_frame h;
    gm_ti0g3bmheuq9o l2; /* settings */
    gm_tl572d7579hns ret;
};
struct gm_fr_apply_option_uqodud5 {
    gm_frame h;
    gm_t04fuf8kbk07t l1; /* hit */
    gm_b0_1 ret;
};
struct gm_fr_canonical_7njb3e6 {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_arguments_are_text_r4m8jtj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_input_files_sg6pbj1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_ogqmkj3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_dir_8brh3n1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_in_fnkagkd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_km5664u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_listener_26qaf3o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_ujrct9b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_command_named_i67jehs {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_compiler_main_rh1a7t4 {
    gm_frame h;
    gm_tdkfkbgm73fql l2; /* opts */
    gm_trqfvmjtt4sij l3; /* files */
    gm_b0_1 ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cpu_count_0o5882g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_current_n0t6630 {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_cwd_5pl4j79 {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_deinit_1jtfr9i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_ab943ap {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8jt91p4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8t7bd38 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fklmfmo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_entry_lp6pm8g {
    gm_frame h;
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_extras_joquqpb {
    gm_frame h;
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fill_rj3q5to {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_find_root_kjnm6hf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_format_of_nbvl1cj {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_heap_allocator_gihki37 {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_help_topic_l4hcqu9 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_install_db2gf1l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_io_result_olh5erq {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_io_unit_ugl1g10 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_is_empty_8l7niee {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kill_mbomb45 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_lint_or_fix_2e18k8s {
    gm_frame h;
    gm_tdkfkbgm73fql l1; /* opts */
    gm_tiaut12ia9v85 l4; /* setup */
    uint8_t ret;
};
struct gm_fr_list_32t1vti {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7h11c55 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bvkp9ir {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_program_4cvaa3s {
    gm_frame h;
    gm_tdkfkbgm73fql l1; /* in_opts */
    gm_tjt4ka19str13 l2; /* a */
    gm_tin97tt4ud0s1 l3; /* sm */
    gm_t2792tsg5eg72 l4; /* diags */
    gm_tdkfkbgm73fql l7; /* opts */
    gm_tcuh1vih4g9cr l9; /* l */
    gm_tcrr6nmqqhjbj l16; /* s */
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_main_6cov5v1 {
    gm_frame h;
    void * l0; /* p */
    gm_th881cfrl0kco l1; /* sched */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_make_dir_n7ma24t {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_mode_of_pq04he2 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_now_bonkgq9 {
    gm_frame h;
    gm_t4ce0jd3tdro4 ret;
};
struct gm_fr_options_2r71b7i {
    gm_frame h;
    gm_tm4vhr17lpr3p ret;
};
struct gm_fr_parse_jobs_e7r1uqv {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_parse_timeout_4hlkio8 {
    gm_frame h;
    double ret;
};
struct gm_fr_peer_tsk9h0n {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_prepare_packages_tttjqji {
    gm_frame h;
    gm_tdkfkbgm73fql l1; /* opts */
    gm_tljis7hgku5bj l3; /* p */
    gm_tctputthhrk9k ret;
};
struct gm_fr_process_file_igvmu0h {
    gm_frame h;
    gm_tdkfkbgm73fql l1; /* in_opts */
    gm_tdkfkbgm73fql l2; /* opts */
    gm_tjt4ka19str13 l5; /* a */
    gm_tin97tt4ud0s1 l6; /* sm */
    gm_t2792tsg5eg72 l8; /* diags */
    gm_tr8dvlu9ik4gj l9; /* lexed */
    gm_trqfvmjtt4sij l10; /* out */
    gm_t9t0nevf2k2cl ret;
    gm_b0_1 w0;
};
struct gm_fr_process_files_1slg8oc {
    gm_frame h;
    gm_tdkfkbgm73fql l1; /* opts */
    gm_b0_1 ret;
};
struct gm_fr_read_72hbrkd {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_read_jvbmtq1 {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_read_link_dr0564a {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_read_options_44df0cc {
    gm_frame h;
    gm_t04fuf8kbk07t l5; /* hit */
    uint8_t ret;
};
struct gm_fr_recv_g45gd8b {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_refuse_freestanding_j543r5e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_remove_0dgs92k {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_remove_dir_66s86sk {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_rename_6qkr5pp {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_render_36ljhn0 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t4gut2c9fj1kt l6; /* d */
    gm_b0_1 ret;
};
struct gm_fr_run_standalone_command_rnlbo5h {
    gm_frame h;
    gm_tvcg39brotose l5; /* o */
    gm_tvcg39brotose l6; /* ro */
    uint8_t ret;
};
struct gm_fr_send_39j26gc {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_set_mode_rr5vrev {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_set_modified_52hvf3d {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_set_timeouts_dubfjtu {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_shutdown_96qqd59 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_sleep_d1vga5f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_source_map_4v8dg4b {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_symlink_6kqaken {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_sync_85b2nq9 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_system_io_mcjcl9m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_timeout_ns_khm4a6r {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_truncate_47rrin9 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_usage_8jc42fu {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* out */
    gm_b16_8 ret;
};
struct gm_fr_usage_error_898tao9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_wall_fid7j1o {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_write_86jor9g {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_write_nkervm9 {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_pr_main_6cov5v1_25 { gm_node *n; };
gm_thf5kr0is9u9i gm_dt3g3ujqvah4_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_1(void *env, gm_node *cx, gm_node *inst, void * a1, void * a2, gm_b16_8 a3, gm_b16_8 a4);
gm_b0_1 gm_dt3ip07hju0a_2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_4(void *env, gm_node *cx, gm_node *inst, void * a1, gm_b16_8 a2);
gm_b0_1 gm_dt3ip07hju0a_5(void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a1, gm_b16_8 a2);
gm_t3mf58gv9kf3n gm_dt3t1u951rul_0(void *env, gm_node *cx, gm_node *inst);
gm_t62ebbi2i5rkd gm_dtaqemfrshlp_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, const void * a2);
gm_thf5kr0is9u9i gm_dtaqemfrshlp_1(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_b0_1 gm_dtaqemfrshlp_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
void gm_dtaqemfrshlp_3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_tlmjd3nndlf3r gm_dtaqemfrshlp_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_tr6norheal11n gm_dtaqemfrshlp_5(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_ttln0d0hd70en gm_dtaqemfrshlp_6(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_t4ce0jd3tdro4 gm_dtf4dspmusp5_0(void *env, gm_node *cx, gm_node *inst);
int64_t gm_dtf4dspmusp5_1(void *env, gm_node *cx, gm_node *inst);
gm_b0_1 gm_dtf4dspmusp5_2(void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a1);
gm_t62ebbi2i5rkd gm_dtffkeesmui4_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, uint16_t a2);
gm_t62ebbi2i5rkd gm_dtffkeesmui4_1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, uint16_t a2);
gm_thf5kr0is9u9i gm_dtffkeesmui4_10(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_tvcqs9u9iqpct gm_dtffkeesmui4_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_t62ebbi2i5rkd gm_dtffkeesmui4_3(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_thf5kr0is9u9i gm_dtffkeesmui4_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_thf5kr0is9u9i gm_dtffkeesmui4_5(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_b0_1 gm_dtffkeesmui4_6(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_b0_1 gm_dtffkeesmui4_7(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_ttln0d0hd70en gm_dtffkeesmui4_8(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_t9qkh0rsooc8d a2);
gm_ttln0d0hd70en gm_dtffkeesmui4_9(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_t6ie8iajlnpt1 a2, gm_t6ie8iajlnpt1 a3);
gm_t62ebbi2i5rkd gm_dtu8v6n5i2qd_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_t4q2b5ibsrdik a2);
gm_b0_1 gm_dtu8v6n5i2qd_1(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_t62ebbi2i5rkd gm_dtu8v6n5i2qd_10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
void gm_dtu8v6n5i2qd_11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_b0_1 gm_dtu8v6n5i2qd_12(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
void gm_dtu8v6n5i2qd_13(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_14(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, uint64_t a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_15(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_16(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_17(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_18(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_19(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_20(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, uint32_t a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_21(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, int64_t a2);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_3(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, int64_t a2, gm_t9qkh0rsooc8d a3);
void gm_dtu8v6n5i2qd_5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dtvslis4o40u_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq7nse29dnpe(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_accept_lrhmdkt(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
__attribute__((aligned(4))) uint32_t gm_f_add_pva1tcq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) void gm_f_apply_build_settings_bc3dtmk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_option_uqodud5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_length_f852tll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_build_vkmrjt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_canonical_7njb3e6(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_arguments_are_text_r4m8jtj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_consts_fsj0q4a(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_input_files_sg6pbj1(void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_program_rltsqqp(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_clear_ogqmkj3(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_dir_8brh3n1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_in_fnkagkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_km5664u(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_listener_26qaf3o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_ujrct9b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
__attribute__((aligned(4))) uint8_t gm_f_command_4iublbb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_command_g3pch81(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_command_je557ls(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_command_named_i67jehs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_command_o05p6ui(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_command_run_06r7r92(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_command_ugm7q4e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_compiler_main_rh1a7t4(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_connect_09fd74a(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint16_t a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_hook_2qtf05o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_core_root_ip442nf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void);
inline __attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_current_n0t6630(void *env, gm_node *cx, gm_node *inst, gm_t3mf58gv9kf3n a0);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_cwd_5pl4j79(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_decl_dump_iah0p30(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_1jtfr9i(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_ab943ap(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_e8lr7h0(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_f4hdl85(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) void gm_f_dependency_dir_grk9phj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_file_cno3fos(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_tokens_4c95gl6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8t7bd38(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fklmfmo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_entry_lp6pm8g(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_exit_ocv9259(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_extras_joquqpb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fill_rj3q5to(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_gl2aoqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l7u3cq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_format_of_nbvl1cj(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_tt07921(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vucln8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void);
__attribute__((aligned(4))) void gm_f_help_topic_l4hcqu9(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_help_topic_l4hcqu9_c(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_host_target_vsu78om(void *gm_out);
inline __attribute__((aligned(4))) void gm_f_install_db2gf1l(void * a0);
__attribute__((aligned(4))) gm_toe5fmr3vvf4d gm_f_io_error_h43olo2(int64_t a0);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_io_result_olh5erq(int64_t a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_io_unit_ugl1g10(int64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_8l7niee(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_package_command_ufjt5rp(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_join_povbqds(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_kill_mbomb45(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_lex_recovering_obo2892(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_lint_or_fix_2e18k8s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_32t1vti(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7h11c55(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_list_rules_tjipqqd(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_listen_etnhveu(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_load_core_du4go5i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_load_modules_2vujekg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_load_package_modules_t4hh6gf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_load_program_4cvaa3s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_root_af4ud7b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_loader_on0up0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) gm_tvcqs9u9iqpct gm_f_local_port_472tpmj(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_lstat_iuafs2d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_main_6cov5v1(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_n7ma24t(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_mode_name_vpt4nrc(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_mode_of_pq04he2(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_new_session_2d945g9(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_next_entry_cv8e316(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t4ce0jd3tdro4 gm_f_now_bonkgq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_open_dir_c8tj9tn(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_open_ebv8fp2(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_t4q2b5ibsrdik a2);
inline __attribute__((aligned(4))) void gm_f_options_2r71b7i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_options_help_tlgad4v(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_park_dgv2f2v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_parse_arg_3o22rma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_file_70g7vok(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_parse_float_17hgksa(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_int_3ce02np(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_parse_jobs_e7r1uqv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_options_9st61k7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) double gm_f_parse_timeout_4hlkio8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_peer_tsk9h0n(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_prepare_build_b1lg1n1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2);
__attribute__((aligned(4))) void gm_f_prepare_packages_tttjqji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, void * a2);
__attribute__((aligned(4))) gm_t9t0nevf2k2cl gm_f_process_file_igvmu0h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_process_files_1slg8oc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_72hbrkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_jvbmtq1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_link_dr0564a(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_read_nba4chq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_read_options_44df0cc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_recv_g45gd8b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_refuse_freestanding_j543r5e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_0dgs92k(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_dir_66s86sk(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_rename_6qkr5pp(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_36ljhn0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_pi7hvmf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_ambient_6orl0vo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_declarations_vrpg1fl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_root_settings_cs4jjnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_run_6mplkoc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_run_command_s4fhqaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_run_dg4eogs(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_run_interp_t6l3qao(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_run_standalone_command_rnlbo5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_scan_ll1gqkn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_scheduler_p4n24jh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_seek_r2chmaj(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, int64_t a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_sema_r1nlnnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_send_39j26gc(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_mode_rr5vrev(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_modified_52hvf3d(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, int64_t a2);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_timeouts_dubfjtu(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t6ie8iajlnpt1 a2, gm_t6ie8iajlnpt1 a3);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_shutdown_96qqd59(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sleep_d1vga5f(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sleep_raw_2i1osko(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_sort_s8jqb1h(void * a0);
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_d42ov39(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_spawn_raw_6afepaq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_stat_ak3228i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_std_root_e0juohb(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_symlink_6kqaken(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_sync_85b2nq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void);
inline __attribute__((aligned(4))) uint64_t gm_f_timeout_ns_khm4a6r(gm_t6ie8iajlnpt1 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_tir_dump_ppa6e9g(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_truncate_47rrin9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, uint64_t a2);
__attribute__((aligned(4))) gm_tr6norheal11n gm_f_try_wait_qpdkkhq(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_of_expr_l8qm9vv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_usage_8jc42fu(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_usage_error_898tao9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tlmjd3nndlf3r gm_f_wait_j3dkg61(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) int64_t gm_f_wall_fid7j1o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_86jor9g(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_nkervm9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_yield_now_bt9oehn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
GM_NOINLINE void gm_pf0nmuupvugn(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE void gm_pfmaojsuds6a(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
void gm_pt_main_6cov5v1_25(void *req, gm_cont k);
void gm_wb_main_6cov5v1_0(gm_fiber *fb);
extern void * gm_x_calloc_vohseaa(uint64_t, uint64_t) __asm__(GM_SYM("calloc"));
extern void gm_x_free_vdscb6h(void *) __asm__(GM_SYM("free"));
extern uint8_t gm_c4i68jc4bv0[16] __attribute__((aligned(8)));
extern uint8_t gm_c50isunhcrj[16] __attribute__((aligned(8)));
extern uint8_t gm_c94f17fblj5[1] __attribute__((aligned(1)));
extern uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
extern uint8_t gm_chtf2n20g83[1] __attribute__((aligned(1)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern uint8_t gm_cjt60aq4qvt[8] __attribute__((aligned(8)));
extern uint8_t gm_cka88d5t8lu[16] __attribute__((aligned(8)));
extern uint8_t gm_cka88d5t8lu_2[16] __attribute__((aligned(8)));
extern uint8_t gm_cka88d5t8lu_3[16] __attribute__((aligned(8)));
extern uint8_t gm_crqb3tjd618[16] __attribute__((aligned(8)));
extern uint8_t gm_ctngbflld19[16] __attribute__((aligned(8)));
extern void *const gm_hd3g3ujqvah4[];
extern void *const gm_hd3ip07hju0a[];
extern void *const gm_hd3t1u951rul[];
extern void *const gm_hdaqemfrshlp[];
extern void *const gm_hdf4dspmusp5[];
extern void *const gm_hdffkeesmui4[];
extern void *const gm_hdu8v6n5i2qd[];
extern void *const gm_hdvslis4o40u[];
extern uint8_t gm_k26b4lfjb04[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s06m3met1mb9v[3];
extern const uint8_t gm_s07iqjq52hems[5];
extern const uint8_t gm_s19jg4ct3aq11[6];
extern const uint8_t gm_s3a1oio07ri1p[3];
extern const uint8_t gm_s4chbmodjkinv[7];
extern const uint8_t gm_s5gaofiqkqtj6[6];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s601j8au4fcin[6];
extern const uint8_t gm_s6iuue4hdrbt9[5];
extern const uint8_t gm_s6rsd5hkg3dmo[6];
extern const uint8_t gm_s7l9fi1pqlcp5[4];
extern const uint8_t gm_s88makc8ms40g[271];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s8rsdirj4dmun[4];
extern const uint8_t gm_s9qbs07c2ca5q[5];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_sa7tr9aaspmun[5];
extern const uint8_t gm_saojq2l3thnfg[14];
extern const uint8_t gm_sdjba9ailb8m1[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sggg4cm5q63i6[5];
extern const uint8_t gm_sh1emun6fmppt[4];
extern const uint8_t gm_sjfs8et3lk0l5[7];
extern const uint8_t gm_sjp9oaluqrtr3[48];
extern const uint8_t gm_sk5htm2k25gsj[16];
extern const uint8_t gm_skijrg68314qg[9];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skou19srual26[10];
extern const uint8_t gm_sl0oflhpdomi7[5];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_slllafvm0pbeb[6];
extern const uint8_t gm_slmlmpuma600i[85];
extern const uint8_t gm_slrfv83pestqp[31];
extern const uint8_t gm_sn2furlv6e7jl[4];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so6kuvn2nvuue[4];
extern const uint8_t gm_soduqpin16t7d[6];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_som446v3b5hhj[176];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sqefppq1so38u[21];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_srbaplcuib5iq[89];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgs96ai7rj93j[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcn623bb2dv5no[];
extern const char gm_srcoj9bno4fi1q4[];
extern const char gm_srcok2brbaepru3[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srfagufvviad7[6];
extern const uint8_t gm_ssfhrckkphjsd[6];
extern const uint8_t gm_stjq3hbrk4g4r[4];
extern const uint8_t gm_su5tga7voctrl[7];
extern const uint8_t gm_sv1gci5lo7scg[5];
extern const uint8_t gm_svtut88rj5dr8[15];
extern const uint8_t gm_svua787vprkp6[8];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq7nse29dnpe(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_allocator_5m783ja F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = t1;
    void * t3 = (void *)((uint8_t *)gm_cjlj93gp0or);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_canonical_7njb3e6(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_canonical_7njb3e6 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_fs_canonical((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8), (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_epo7cln F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_f_canonicalize_n3e3qri((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) void gm_f_clear_ogqmkj3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_ogqmkj3 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 184)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_dir_8brh3n1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_dir_8brh3n1 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* d */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_fs_close_dir(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_in_fnkagkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_in_fnkagkd F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_proc_close_in(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_km5664u(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_km5664u F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_fs_close(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_listener_26qaf3o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_listener_26qaf3o F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* l */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_net_close(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_ujrct9b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_ujrct9b F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_net_close(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    uint8_t t4;
    if (((((uint8_t *)&t3))[8] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cpu_count_0o5882g F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint32_t t1 = gm_cpu_count();
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_current_n0t6630(void *env, gm_node *cx, gm_node *inst, gm_t3mf58gv9kf3n a0) {
    struct gm_fr_current_n0t6630 F;
    gm_t3mf58gv9kf3n gm_l0; /* self */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t3mf58gv9kf3n t1 = gm_l0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_cwd_5pl4j79(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_cwd_5pl4j79 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* buf */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    int64_t t2 = gm_fs_cwd((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8));
    gm_thf5kr0is9u9i t3 = gm_f_io_result_olh5erq(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_deinit_1jtfr9i(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_1jtfr9i F;
    void * gm_l0; /* h */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 0);
        gm_f_deinit_e8lr7h0(t3);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_ab943ap(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_ab943ap F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 0);
        gm_f_deinit_f4hdl85(t3);
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 48);
        gm_f_deinit_1jtfr9i(t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 60, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_prbqo9j((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 40, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 48, ((uint64_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 56, ((uint32_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8jt91p4 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8t7bd38(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8t7bd38 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fklmfmo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fklmfmo F;
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
inline __attribute__((aligned(4))) void gm_f_extras_joquqpb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_extras_joquqpb F;
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_7h11c55((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t2;
    gm_f_list_epvducg((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_epvducg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 0, t1);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 40, t2);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 80, t3);
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fill_rj3q5to(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_fill_rj3q5to F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* buf */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_rand_fill((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8));
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_kjnm6hf F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_th2lqbo77pvsc t4;
        gm_f_canonicalize_epo7cln((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t5;
        if (((((uint8_t *)&t4))[16] != 0)) goto mn7;
        t5 = ((uint8_t)1);
        goto me6;
mn7:;
        t5 = ((uint8_t)0);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 11, 8);
me6:;
        if (!t5) goto ie2;
        {
            gm_th2lqbo77pvsc t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t12 = gm_l0;
        gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t12);
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_format_of_nbvl1cj(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_format_of_nbvl1cj F;
    gm_b16_8 gm_l0; /* value */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_ssfhrckkphjsd, 5) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
    t1 = t4;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sggg4cm5q63i6, 4) == 0)) goto mn5;
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)2ULL));
    t1 = t6;
    goto me2;
mn5:;
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
    t1 = t8;
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn623bb2dv5no, 322, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 40));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_allocator_gihki37 F;
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t1 = (void *)((uint8_t *)gm_cgd1khc5sal);
    void * t2 = (void *)((uint8_t *)gm_cgd1khc5sal);
    gm_t3mf58gv9kf3n t3;
    GM_ST(void *, ((uint8_t *)&t3 + 0), t1);
    GM_ST(void *, ((uint8_t *)&t3 + 8), t2);
    F.ret = t3;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_install_db2gf1l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_install_db2gf1l F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        GM_ST(void *, (((uint8_t *)t2) + 2168), ((void *)gm_f_const_hook_2qtf05o));
    }
    {
        void * t3 = gm_l0;
        GM_ST(void *, (((uint8_t *)t3) + 2176), ((void *)gm_f_array_length_f852tll));
    }
    {
        void * t4 = gm_l0;
        GM_ST(void *, (((uint8_t *)t4) + 2184), ((void *)gm_f_type_of_expr_l8qm9vv));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_io_result_olh5erq(int64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_io_result_olh5erq F;
    int64_t gm_l0; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        int64_t t3 = gm_l0;
        uint8_t t4 = (t3 >= ((int64_t)0LL));
        if (!t4) goto ie2;
        {
            int64_t t6 = gm_l0;
            uint64_t t7 = (uint64_t)t6;
            gm_thf5kr0is9u9i t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t8 + 8), t7);
            uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        int64_t t10 = gm_l0;
        gm_toe5fmr3vvf4d t11 = gm_f_io_error_h43olo2(t10);
        gm_thf5kr0is9u9i t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
        ((uint8_t *)&t12)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t12 + 8), t11);
        uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_io_unit_ugl1g10(int64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_io_unit_ugl1g10 F;
    int64_t gm_l0; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            int64_t t3 = gm_l0;
            gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
            if (((((uint8_t *)&t4))[0] != 0)) {
                gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
                gm_ttln0d0hd70en t6;
                GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
                ((uint8_t *)&t6)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t6 + 4), t5);
                F.ret = t6;
                goto gm_fret;
            }
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
        }
    }
    {
        gm_b0_1 t8;
        gm_ttln0d0hd70en t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 8, 0);
        ((uint8_t *)&t9)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t9 + 4), t8);
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_8l7niee(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_8l7niee F;
    void * gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 40) + 0) + 8);
        uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
        t2 = t8;
    } else t2 = 0;
    if (t2) {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 80) + 0) + 8);
        uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
        t1 = t11;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_kill_mbomb45(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_kill_mbomb45 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* c */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    int64_t t2 = gm_proc_kill(t1);
    gm_ttln0d0hd70en t3 = gm_f_io_unit_ugl1g10(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_dc8mbmf F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_f_new_lexer_dm12g12((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l3);
        gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t5);
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
inline __attribute__((aligned(4))) void gm_f_list_32t1vti(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_32t1vti F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8t7bd38();
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
inline __attribute__((aligned(4))) void gm_f_list_7h11c55(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7h11c55 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fklmfmo();
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
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bvkp9ir F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8jt91p4();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 200) + 0) + 8);
    if (t2 >= (uintptr_t)(t4)) gm_trap_index(t2, t4, gm_srcc2qutubarto1, 547, 78);
    void * t5 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 200) + 0)) + t2 * 184) + 56);
    gm_b16_8 t6 = gm_l2;
    gm_tf5srh4mp4b3i t7 = gm_f_get_espvf96(t5, t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_n7ma24t(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_make_dir_n7ma24t F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    int64_t t2 = gm_fs_make_dir((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8));
    gm_ttln0d0hd70en t3 = gm_f_io_unit_ugl1g10(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_mode_of_pq04he2(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mode_of_pq04he2 F;
    gm_b16_8 gm_l0; /* value */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s02419ud122hi, 7) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
    t1 = t4;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s6iuue4hdrbt9, 4) == 0)) goto mn5;
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)2ULL));
    t1 = t6;
    goto me2;
mn5:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sl0oflhpdomi7, 4) == 0)) goto mn7;
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)3ULL));
    t1 = t8;
    goto me2;
mn7:;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
    t1 = t10;
    goto me2;
mn9:;
    gm_trap("no match arm matched the scrutinee", gm_srcn623bb2dv5no, 314, 45);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_jr5ejs0 F;
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_node *pn1 = gm_find(cx, 587034433);
    if (!pn1) gm_trap("no handler installed for effect `Alloc`", gm_srcip69en21ht7v, 41, 55);
    gm_t3mf58gv9kf3n t2;
    t2 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn1->clauses[0])(pn1->env, pn1->hctx, pn1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 0, t2);
    GM_ST(void *, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 16, ((void *)0));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 24, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 32, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 40, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) gm_t4ce0jd3tdro4 gm_f_now_bonkgq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0) {
    struct gm_fr_now_bonkgq9 F;
    gm_b0_1 gm_l0; /* self */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_clock_now();
    gm_t4ce0jd3tdro4 t2;
    GM_ST(uint64_t, ((uint8_t *)&t2 + 0), t1);
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_options_2r71b7i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_options_2r71b7i F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tm4vhr17lpr3p *)gm_out) + 3, 0, 5);
    gm_trqfvmjtt4sij t1;
    gm_f_list_32t1vti((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tm4vhr17lpr3p *)gm_out) + 0, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tm4vhr17lpr3p *)gm_out) + 1, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tm4vhr17lpr3p *)gm_out) + 2, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tm4vhr17lpr3p *)gm_out) + 8, t1);
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
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_peer_tsk9h0n(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_peer_tsk9h0n F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_net_peer(t1, (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_72hbrkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_read_72hbrkd F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_fs_read(t1, (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_jvbmtq1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_read_jvbmtq1 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* buf */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    int64_t t2 = gm_sys_read((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8));
    gm_thf5kr0is9u9i t3 = gm_f_io_result_olh5erq(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_link_dr0564a(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_read_link_dr0564a F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_fs_read_link((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8), (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_recv_g45gd8b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_recv_g45gd8b F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_net_recv(t1, (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_refuse_freestanding_j543r5e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_refuse_freestanding_j543r5e F;
    void * gm_l0; /* opts */
    gm_b16_8 gm_l1; /* command */
    GM_STACK_CHECK(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = (!((GM_LD(uintptr_t, ((((uint8_t *)t3) + 400) + 24) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)t3) + 400) + 24)), gm_soo4rtckloi4d, 12) == 0));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t7 = gm_l1;
        if (gm_pf454qfrpt6r(cx, gm_svtut88rj5dr8, 14, gm_srcok2brbaepru3, 55, 5)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t7) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t7) + 8), gm_srcok2brbaepru3, 55, 5))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_s88makc8ms40g, 270, gm_srcok2brbaepru3, 55, 5)) {
            goto gm_lad;
        }
    }
    {
        uint8_t t8 = 0;
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
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_0dgs92k(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_remove_0dgs92k F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    int64_t t2 = gm_fs_remove((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8));
    gm_ttln0d0hd70en t3 = gm_f_io_unit_ugl1g10(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_dir_66s86sk(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_remove_dir_66s86sk F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    int64_t t2 = gm_fs_remove_dir((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8));
    gm_ttln0d0hd70en t3 = gm_f_io_unit_ugl1g10(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_rename_6qkr5pp(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_rename_6qkr5pp F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* from */
    gm_b16_8 gm_l2; /* to */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_fs_rename((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8), (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_ttln0d0hd70en t4 = gm_f_io_unit_ugl1g10(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_send_39j26gc(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_send_39j26gc F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_b16_8 gm_l2; /* bytes */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_net_send(t1, (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_mode_rr5vrev(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint32_t a2) {
    struct gm_fr_set_mode_rr5vrev F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    uint32_t gm_l2; /* mode */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    uint32_t t2 = gm_l2;
    int64_t t3 = gm_fs_set_mode((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8), t2);
    gm_ttln0d0hd70en t4 = gm_f_io_unit_ugl1g10(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_modified_52hvf3d(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, int64_t a2) {
    struct gm_fr_set_modified_52hvf3d F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    int64_t gm_l2; /* ns */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    int64_t t2 = gm_l2;
    int64_t t3 = gm_fs_set_modified((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8), t2);
    gm_ttln0d0hd70en t4 = gm_f_io_unit_ugl1g10(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_timeouts_dubfjtu(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t6ie8iajlnpt1 a2, gm_t6ie8iajlnpt1 a3) {
    struct gm_fr_set_timeouts_dubfjtu F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_t6ie8iajlnpt1 gm_l2; /* recv */
    gm_t6ie8iajlnpt1 gm_l3; /* send */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_t6ie8iajlnpt1 t2 = gm_l2;
    uint64_t t3 = gm_f_timeout_ns_khm4a6r(t2);
    gm_t6ie8iajlnpt1 t4 = gm_l3;
    uint64_t t5 = gm_f_timeout_ns_khm4a6r(t4);
    int64_t t6 = gm_net_set_timeouts(t1, t3, t5);
    gm_ttln0d0hd70en t7 = gm_f_io_unit_ugl1g10(t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_shutdown_96qqd59(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_shutdown_96qqd59 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_t9qkh0rsooc8d gm_l2; /* how */
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_t9qkh0rsooc8d t2 = gm_l2;
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&t2));
    uint32_t t4 = (uint32_t)t3;
    int64_t t5 = gm_net_shutdown(t1, t4);
    gm_ttln0d0hd70en t6 = gm_f_io_unit_ugl1g10(t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sleep_d1vga5f(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_t4ce0jd3tdro4 a1) {
    struct gm_fr_sleep_d1vga5f F;
    gm_b0_1 gm_l0; /* self */
    gm_t4ce0jd3tdro4 gm_l1; /* d */
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
    gm_clock_sleep(t1);
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_source_map_4v8dg4b F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_bvkp9ir((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tin97tt4ud0s1 *)gm_out) + 0, t1);
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
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_symlink_6kqaken(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_symlink_6kqaken F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* target */
    gm_b16_8 gm_l2; /* path */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_fs_symlink((void *)GM_LD(void *, ((uint8_t *)&t1)), GM_LD(uintptr_t, ((uint8_t *)&t1) + 8), (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_ttln0d0hd70en t4 = gm_f_io_unit_ugl1g10(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_sync_85b2nq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_sync_85b2nq9 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    int64_t t2 = gm_fs_sync(t1);
    gm_ttln0d0hd70en t3 = gm_f_io_unit_ugl1g10(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_io_mcjcl9m F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b0_1 t1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_timeout_ns_khm4a6r(gm_t6ie8iajlnpt1 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_timeout_ns_khm4a6r F;
    gm_t6ie8iajlnpt1 gm_l0; /* d */
    gm_t4ce0jd3tdro4 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        if (!((((uint8_t *)&gm_l0))[8] != 0)) goto ie2;
        gm_t4ce0jd3tdro4 t3 = GM_LD(gm_t4ce0jd3tdro4, ((uint8_t *)&gm_l0));
        gm_l1 = t3;
        {
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
            uint64_t t6 = (t5 > ((uint64_t)1ULL) ? t5 : ((uint64_t)1ULL));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t8 = 0;
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_truncate_47rrin9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, uint64_t a2) {
    struct gm_fr_truncate_47rrin9 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    uint64_t gm_l2; /* size */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = gm_l2;
    int64_t t3 = gm_fs_truncate(t1, t2);
    gm_ttln0d0hd70en t4 = gm_f_io_unit_ugl1g10(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_wall_fid7j1o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0) {
    struct gm_fr_wall_fid7j1o F;
    gm_b0_1 gm_l0; /* self */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1 = gm_clock_wall();
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_86jor9g(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_write_86jor9g F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* c */
    gm_b16_8 gm_l2; /* bytes */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_proc_write_in(t1, (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_nkervm9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_write_nkervm9 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    gm_b16_8 gm_l2; /* bytes */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    int64_t t3 = gm_fs_write(t1, (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
gm_thf5kr0is9u9i gm_dt3g3ujqvah4_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_read_jvbmtq1(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_b0_1 gm_dt3ip07hju0a_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_yield_now_bt9oehn(NULL, cx, inst, GM_LD(void *, env), a1); }
gm_b0_1 gm_dt3ip07hju0a_1(void *env, gm_node *cx, gm_node *inst, void * a1, void * a2, gm_b16_8 a3, gm_b16_8 a4) { return gm_f_spawn_raw_6afepaq(NULL, cx, inst, GM_LD(void *, env), a1, a2, a3, a4); }
gm_b0_1 gm_dt3ip07hju0a_2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_exit_ocv9259(NULL, cx, inst, GM_LD(void *, env), a1); }
gm_b0_1 gm_dt3ip07hju0a_3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_join_povbqds(NULL, cx, inst, GM_LD(void *, env), a1); }
gm_b0_1 gm_dt3ip07hju0a_4(void *env, gm_node *cx, gm_node *inst, void * a1, gm_b16_8 a2) { return gm_f_park_dgv2f2v(NULL, cx, inst, GM_LD(void *, env), a1, a2); }
gm_b0_1 gm_dt3ip07hju0a_5(void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a1, gm_b16_8 a2) { return gm_f_sleep_raw_2i1osko(NULL, cx, inst, GM_LD(void *, env), a1, a2); }
gm_t3mf58gv9kf3n gm_dt3t1u951rul_0(void *env, gm_node *cx, gm_node *inst) { return gm_f_current_n0t6630(NULL, cx, inst, GM_LD(gm_t3mf58gv9kf3n, env)); }
gm_t62ebbi2i5rkd gm_dtaqemfrshlp_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, const void * a2) { return gm_f_spawn_d42ov39(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_thf5kr0is9u9i gm_dtaqemfrshlp_1(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2) { return gm_f_write_86jor9g(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_b0_1 gm_dtaqemfrshlp_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_close_in_fnkagkd(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
void gm_dtaqemfrshlp_3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2) { gm_f_read_nba4chq(gm_out, NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_tlmjd3nndlf3r gm_dtaqemfrshlp_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_wait_j3dkg61(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_tr6norheal11n gm_dtaqemfrshlp_5(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_try_wait_qpdkkhq(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_ttln0d0hd70en gm_dtaqemfrshlp_6(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_kill_mbomb45(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_t4ce0jd3tdro4 gm_dtf4dspmusp5_0(void *env, gm_node *cx, gm_node *inst) { return gm_f_now_bonkgq9(NULL, cx, inst, GM_LD(gm_b0_1, env)); }
int64_t gm_dtf4dspmusp5_1(void *env, gm_node *cx, gm_node *inst) { return gm_f_wall_fid7j1o(NULL, cx, inst, GM_LD(gm_b0_1, env)); }
gm_b0_1 gm_dtf4dspmusp5_2(void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a1) { return gm_f_sleep_d1vga5f(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_t62ebbi2i5rkd gm_dtffkeesmui4_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, uint16_t a2) { return gm_f_connect_09fd74a(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_t62ebbi2i5rkd gm_dtffkeesmui4_1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, uint16_t a2) { return gm_f_listen_etnhveu(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_thf5kr0is9u9i gm_dtffkeesmui4_10(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2) { return gm_f_peer_tsk9h0n(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_tvcqs9u9iqpct gm_dtffkeesmui4_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_local_port_472tpmj(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_t62ebbi2i5rkd gm_dtffkeesmui4_3(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_accept_lrhmdkt(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_thf5kr0is9u9i gm_dtffkeesmui4_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2) { return gm_f_send_39j26gc(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_thf5kr0is9u9i gm_dtffkeesmui4_5(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2) { return gm_f_recv_g45gd8b(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_b0_1 gm_dtffkeesmui4_6(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_close_ujrct9b(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_b0_1 gm_dtffkeesmui4_7(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_close_listener_26qaf3o(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_ttln0d0hd70en gm_dtffkeesmui4_8(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_t9qkh0rsooc8d a2) { return gm_f_shutdown_96qqd59(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_ttln0d0hd70en gm_dtffkeesmui4_9(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_t6ie8iajlnpt1 a2, gm_t6ie8iajlnpt1 a3) { return gm_f_set_timeouts_dubfjtu(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2, a3); }
gm_t62ebbi2i5rkd gm_dtu8v6n5i2qd_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_t4q2b5ibsrdik a2) { return gm_f_open_ebv8fp2(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_b0_1 gm_dtu8v6n5i2qd_1(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_close_km5664u(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_t62ebbi2i5rkd gm_dtu8v6n5i2qd_10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_open_dir_c8tj9tn(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
void gm_dtu8v6n5i2qd_11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2) { gm_f_next_entry_cv8e316(gm_out, NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_b0_1 gm_dtu8v6n5i2qd_12(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_close_dir_8brh3n1(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
void gm_dtu8v6n5i2qd_13(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { gm_f_lstat_iuafs2d(gm_out, NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_14(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, uint64_t a2) { return gm_f_truncate_47rrin9(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_15(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1) { return gm_f_sync_85b2nq9(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_16(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_cwd_5pl4j79(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_17(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2) { return gm_f_canonical_7njb3e6(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_18(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2) { return gm_f_read_link_dr0564a(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_19(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2) { return gm_f_symlink_6kqaken(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2) { return gm_f_read_72hbrkd(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_20(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, uint32_t a2) { return gm_f_set_mode_rr5vrev(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_21(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, int64_t a2) { return gm_f_set_modified_52hvf3d(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_3(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2) { return gm_f_write_nkervm9(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, int64_t a2, gm_t9qkh0rsooc8d a3) { return gm_f_seek_r2chmaj(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2, a3); }
void gm_dtu8v6n5i2qd_5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { gm_f_stat_ak3228i(gm_out, NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_remove_0dgs92k(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2) { return gm_f_rename_6qkr5pp(NULL, cx, inst, GM_LD(gm_b0_1, env), a1, a2); }
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_make_dir_n7ma24t(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_remove_dir_66s86sk(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
gm_b0_1 gm_dtvslis4o40u_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1) { return gm_f_fill_rj3q5to(NULL, cx, inst, GM_LD(gm_b0_1, env), a1); }
inline __attribute__((aligned(4))) void gm_f_apply_build_settings_bc3dtmk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_apply_build_settings_bc3dtmk F;
    gm_b16_8 gm_l0; /* input */
    void * gm_l1; /* opts */
    gm_t9qkh0rsooc8d gm_l3; /* m */
    uint8_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 667u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t9h00sl1nspi5 t4;
        gm_f_root_settings_cs4jjnk((void *)&t4, (void *)NULL, cx, NULL, t2, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)t3) + 184)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            gm_tl572d7579hns t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_ti0g3bmheuq9o t7;
        if ((((((uint8_t *)&t4) + 8))[152] != 0)) {
            t7 = (*(gm_ti0g3bmheuq9o *)(((uint8_t *)&t4) + 8));
        } else {
            gm_b0_1 t8;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        F.l2 = t7;
    }
    {
        void * t10 = gm_l1;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t10) + 208));
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie9;
        {
            if (!(((((uint8_t *)&F.l2) + 0))[1] != 0)) goto ie14;
            gm_t9qkh0rsooc8d t15 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 0));
            gm_l3 = t15;
            {
                void * t17 = gm_l1;
                gm_t9qkh0rsooc8d t18;
                uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                switch (t25) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma23; default: goto ma24; }
ma20:;
                uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t27 != ((uint8_t)0ULL)) goto mn26;
                gm_t9qkh0rsooc8d t28;
                GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
                t18 = t28;
                goto me19;
mn26:;
ma21:;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t30 != ((uint8_t)1ULL)) goto mn29;
                gm_t9qkh0rsooc8d t31;
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)1ULL));
                t18 = t31;
                goto me19;
mn29:;
ma22:;
                uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t33 != ((uint8_t)2ULL)) goto mn32;
                gm_t9qkh0rsooc8d t34;
                GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)2ULL));
                t18 = t34;
                goto me19;
mn32:;
ma23:;
                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t36 != ((uint8_t)3ULL)) goto mn35;
                gm_t9qkh0rsooc8d t37;
                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)3ULL));
                t18 = t37;
                goto me19;
mn35:;
ma24:;
                gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 428, 25);
me19:;
                GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t17) + 4), t18);
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t39 = gm_l1;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t39) + 34));
        uint8_t t41 = (uint8_t)!t40;
        if (!t41) goto ie38;
        {
            if (!(((((uint8_t *)&F.l2) + 2))[1] != 0)) goto ie43;
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 2));
            gm_l4 = t44;
            {
                void * t46 = gm_l1;
                uint8_t t47 = gm_l4;
                GM_ST(uint8_t, (((uint8_t *)t46) + 33), t47);
            }
            goto ix43;
ie43:;
ix43:;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        void * t48 = gm_l1;
        uint8_t *t49 = (((uint8_t *)t48) + 216);
        gm_th2lqbo77pvsc t50 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 8));
        gm_b16_8 t51;
        if (((((uint8_t *)&t50))[16] != 0)) {
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 0));
            t51 = t52;
        } else {
            gm_b16_8 t53 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            t51 = t54;
        }
        GM_ST(gm_b16_8, t49, t51);
    }
    {
        void * t55 = gm_l1;
        uint8_t *t56 = (((uint8_t *)t55) + 232);
        gm_th2lqbo77pvsc t57 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 32));
        gm_b16_8 t58;
        if (((((uint8_t *)&t57))[16] != 0)) {
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 0));
            t58 = t59;
        } else {
            gm_b16_8 t60 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            t58 = t61;
        }
        GM_ST(gm_b16_8, t56, t58);
    }
    {
        gm_b0_1 t62;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t62);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_option_uqodud5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_apply_option_uqodud5 F;
    void * gm_l0; /* opts */
    gm_b16_8 gm_l2; /* value */
    gm_b16_8 gm_l3; /* file */
    GM_STACK_CHECK(&F, 619u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_th2lqbo77pvsc t2 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 8));
        gm_b16_8 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
            t3 = t5;
        }
        gm_l2 = t3;
    }
    {
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        switch (t29) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma27; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma19; case ((uint8_t)14ULL): goto ma20; case ((uint8_t)15ULL): goto ma21; case ((uint8_t)16ULL): goto ma22; case ((uint8_t)17ULL): goto ma23; case ((uint8_t)18ULL): goto ma24; case ((uint8_t)19ULL): goto ma25; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma27; default: goto ma28; }
ma7:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t31 != ((uint8_t)4ULL)) goto mn30;
        {
            void * t33 = gm_l0;
            uint8_t *t34 = (((uint8_t *)t33) + 4);
            gm_b16_8 t35 = gm_l2;
            gm_t9qkh0rsooc8d t36 = gm_f_mode_of_pq04he2(t35);
            GM_ST(gm_t9qkh0rsooc8d, t34, t36);
        }
        {
            void * t37 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t37) + 208), ((uint8_t)1));
        }
        goto me6;
mn30:;
ma8:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t39 != ((uint8_t)0ULL)) goto mn38;
        {
            void * t41 = gm_l0;
            uint8_t *t42 = (((uint8_t *)t41) + 1);
            gm_b16_8 t43 = gm_l2;
            gm_t9qkh0rsooc8d t44 = gm_f_format_of_nbvl1cj(t43);
            GM_ST(gm_t9qkh0rsooc8d, t42, t44);
        }
        goto me6;
mn38:;
ma9:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t46 != ((uint8_t)2ULL)) goto mn45;
        {
            void * t48 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t48) + 2), ((uint8_t)1));
        }
        goto me6;
mn45:;
ma10:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t50 != ((uint8_t)3ULL)) goto mn49;
        {
            void * t52 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t52) + 3), ((uint8_t)1));
        }
        goto me6;
mn49:;
ma11:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t54 != ((uint8_t)5ULL)) goto mn53;
        {
            void * t57 = gm_l0;
            gm_t9qkh0rsooc8d t58 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t57) + 0));
            gm_t9qkh0rsooc8d t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)6ULL));
            gm_t9qkh0rsooc8d t60;
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)7ULL));
            gm_t0am46iohlpol t61;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t61 + 0), t59);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t61 + 1), t60);
            uint8_t t62 = 0;
            if (gm_eq7nse29dnpe(((uint8_t *)&t61) + 0, ((uint8_t *)&t58))) t62 = 1;
            if (gm_eq7nse29dnpe(((uint8_t *)&t61) + 1, ((uint8_t *)&t58))) t62 = 1;
            uint8_t t63 = (uint8_t)(t62);
            if (!t63) goto ie56;
            {
                gm_b16_8 t65;
                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sjp9oaluqrtr3));
                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(47));
                gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix56;
ie56:;
ix56:;
        }
        {
            void * t66 = gm_l0;
            gm_b16_8 t67 = gm_l2;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t66) + 376)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t66) + 376)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t66) + 376)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t66) + 376)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t66) + 376)) + 0, t67);
        }
        goto me6;
mn53:;
ma12:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t69 != ((uint8_t)6ULL)) goto mn68;
        {
            void * t71 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t71) + 33), ((uint8_t)1));
        }
        {
            void * t72 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t72) + 34), ((uint8_t)1));
        }
        goto me6;
mn68:;
ma13:;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t74 != ((uint8_t)7ULL)) goto mn73;
        {
            void * t76 = gm_l0;
            gm_b16_8 t77 = gm_l2;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t76) + 8)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t76) + 8)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t76) + 8)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t76) + 8)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t76) + 8)) + 0, t77);
        }
        goto me6;
mn73:;
ma14:;
        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t79 != ((uint8_t)8ULL)) goto mn78;
        {
            void * t81 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t81) + 32), ((uint8_t)1));
        }
        goto me6;
mn78:;
ma15:;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t83 != ((uint8_t)9ULL)) goto mn82;
        {
            void * t85 = gm_l0;
            gm_b16_8 t86 = gm_l2;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t85) + 64)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t85) + 64)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t85) + 64)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t85) + 64)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t85) + 64)) + 0, t86);
        }
        goto me6;
mn82:;
ma16:;
        uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t88 != ((uint8_t)10ULL)) goto mn87;
        {
            void * t90 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t90) + 88), ((uint8_t)1));
        }
        goto me6;
mn87:;
ma17:;
        uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t92 != ((uint8_t)11ULL)) goto mn91;
        {
            void * t94 = gm_l0;
            gm_b16_8 t95 = gm_l2;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t94) + 152)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t94) + 152)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t94) + 152)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t94) + 152)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t94) + 152)) + 0, t95);
        }
        goto me6;
mn91:;
ma18:;
        uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t97 != ((uint8_t)12ULL)) goto mn96;
        {
            void * t99 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t99) + 89), ((uint8_t)1));
        }
        goto me6;
mn96:;
ma19:;
        uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t101 != ((uint8_t)13ULL)) goto mn100;
        {
            void * t103 = gm_l0;
            uint8_t *t104 = (((uint8_t *)t103) + 96);
            gm_b16_8 t105 = gm_l2;
            uint64_t t106 = gm_f_parse_jobs_e7r1uqv((void *)NULL, cx, NULL, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, t104, t106);
        }
        goto me6;
mn100:;
ma20:;
        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t108 != ((uint8_t)14ULL)) goto mn107;
        {
            void * t110 = gm_l0;
            uint8_t *t111 = (((uint8_t *)t110) + 104);
            gm_b16_8 t112 = gm_l2;
            double t113 = gm_f_parse_timeout_4hlkio8((void *)NULL, cx, NULL, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2bqc58ss467 t114;
            GM_ST(uint8_t, (uint8_t *)&t114 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t114 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t114 + 12, 0);
            GM_ST(double, ((uint8_t *)&t114), t113);
            ((uint8_t *)&t114)[8] = 1;
            GM_ST(gm_th2bqc58ss467, t111, t114);
        }
        goto me6;
mn107:;
ma21:;
        uint8_t t116 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t116 != ((uint8_t)15ULL)) goto mn115;
        {
            void * t118 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t118) + 120), ((uint8_t)1));
        }
        goto me6;
mn115:;
ma22:;
        uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t120 != ((uint8_t)16ULL)) goto mn119;
        {
            void * t122 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t122) + 121), ((uint8_t)1));
        }
        {
            if (!(((((uint8_t *)a1) + 8))[16] != 0)) goto ie123;
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
            gm_l3 = t124;
            {
                void * t126 = gm_l0;
                gm_b16_8 t127 = gm_l3;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t126) + 128)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t126) + 128)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t126) + 128)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t126) + 128)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t126) + 128)) + 0, t127);
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto me6;
mn119:;
ma23:;
        uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t129 != ((uint8_t)17ULL)) goto mn128;
        {
            uint8_t t132 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sjfs8et3lk0l5, 6) == 0));
            if (!t132) goto ie131;
            void * t133 = gm_l0;
            uint8_t t134 = (!(GM_LD(uint8_t, (((uint8_t *)t133) + 0) + 0) == ((uint8_t)9ULL)));
            if (!t134) goto ie131;
            {
                gm_b16_8 t136;
                GM_ST(void *, (uint8_t *)&t136, (void *)(gm_slmlmpuma600i));
                GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(84));
                gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t136);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix131;
ie131:;
ix131:;
        }
        {
            void * t137 = gm_l0;
            uint8_t t138 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sjfs8et3lk0l5, 6) == 0));
            GM_ST(uint8_t, (((uint8_t *)t137) + 56), t138);
        }
        goto me6;
mn128:;
ma24:;
        uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t140 != ((uint8_t)18ULL)) goto mn139;
        {
            void * t142 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t142) + 176), ((uint8_t)1));
        }
        goto me6;
mn139:;
ma25:;
        uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t144 != ((uint8_t)19ULL)) goto mn143;
        {
            void * t146 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t146) + 177), ((uint8_t)1));
        }
        goto me6;
mn143:;
ma26:;
        uint8_t t148 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t148 != ((uint8_t)20ULL)) goto mn147;
        {
            void * t150 = gm_l0;
            gm_b16_8 t151 = gm_l2;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t150) + 184)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t150) + 184)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t150) + 184)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t150) + 184)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t150) + 184)) + 0, t151);
        }
        goto me6;
mn147:;
ma27:;
        uint8_t t155 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t155 != ((uint8_t)1ULL)) goto pa154;
        goto po153;
pa154:;
        uint8_t t157 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t157 != ((uint8_t)21ULL)) goto pa156;
        goto po153;
pa156:;
        goto mn152;
po153:;
        goto me6;
mn152:;
ma28:;
        gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 272, 5);
me6:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_arguments_are_text_r4m8jtj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_check_arguments_are_text_r4m8jtj F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* command */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* a */
    GM_STACK_CHECK(&F, 424u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
        uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
        uint64_t t5 = (uintptr_t)((uint64_t)1ULL);
        gm_th2lqbo77pvsc t6;
        if (t5 < t4) {
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)t3 + t5 * 16));
        gm_th2lqbo77pvsc t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[16] = 1;
            t6 = t8;
        } else {
        gm_th2lqbo77pvsc t9;
        memset(&t9, 0, sizeof t9);
            t6 = t9;
        }
        gm_b16_8 t10;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t10 = t11;
        } else {
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l1 = t10;
    }
    {
        gm_l2 = ((uint64_t)1ULL);
    }
    {
        for (;;) {
            uint64_t t15 = gm_l2;
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t17 = (t15 < t16);
            if (!t17) goto lb14;
            {
                uint64_t t20 = gm_l2;
                uint64_t t21 = (uintptr_t)t20;
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcgs96ai7rj93j, 361, 14);
                gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t21 * 16));
                gm_l3 = t23;
            }
            {
                gm_b16_8 t25 = gm_l3;
                uint8_t t26 = gm_f_utf8_valid_a8oc1t8(t25);
                uint8_t t27 = (uint8_t)!t26;
                if (!t27) goto ie24;
                {
                    uint64_t t29 = gm_l2;
                    gm_t4ce0jd3tdro4 t30;
                    GM_ST(uint64_t, ((uint8_t *)&t30 + 0), t29);
                    gm_tabb710d9mq2e t31;
                    gm_f_format_vucln8v((void *)&t31, (void *)NULL, cx, NULL, t30);
                    if (gm_unw.kind) {
                        goto gm_ll18;
                    }
                    if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcgs96ai7rj93j, 363, 25);
                    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                    gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t33);
                    if (gm_unw.kind) {
                        goto gm_ll18;
                    }
                }
                goto ix24;
ie24:;
ix24:;
            }
            {
                uint64_t t34 = gm_l2;
                uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                if (o36) gm_trap("integer overflow in `+`", gm_srcgs96ai7rj93j, 365, 9);
                uint64_t t37 = r35;
                gm_l2 = t37;
            }
            {
                uint8_t t39 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sn2furlv6e7jl, 3) == 0));
                if (!t39) goto ie38;
                uint64_t t40 = gm_l2;
                uint8_t t41 = (t40 > ((uint64_t)2ULL));
                if (!t41) goto ie38;
                gm_b16_8 t42 = gm_l3;
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(1));
                uint8_t t44 = gm_f_starts_with_6vvt01e(t42, t43);
                uint8_t t45 = (uint8_t)!t44;
                if (!t45) goto ie38;
                {
                    goto lb14;
                }
                goto ix38;
ie38:;
ix38:;
            }
lc14:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb14; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc14; }
        }
        goto gm_lad;
lb14:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_input_files_sg6pbj1(void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, gm_b16_8 a1) {
    struct gm_fr_check_input_files_sg6pbj1 F;
    gm_t9qkh0rsooc8d gm_l0; /* command */
    gm_b16_8 gm_l1; /* files */
    GM_STACK_CHECK(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_saojq2l3thnfg));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(13));
            gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t9qkh0rsooc8d t8 = gm_l0;
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)8ULL));
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)9ULL));
        gm_t9qkh0rsooc8d t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)6ULL));
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)7ULL));
        gm_t9qkh0rsooc8d t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)10ULL));
        gm_t9qkh0rsooc8d t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)11ULL));
        gm_t9rbkbpjtolar t15;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 0), t9);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t10);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 2), t11);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 3), t12);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 4), t13);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 5), t14);
        uint8_t t16 = 0;
        if (gm_eq7nse29dnpe(((uint8_t *)&t15) + 0, ((uint8_t *)&t8))) t16 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t15) + 1, ((uint8_t *)&t8))) t16 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t15) + 2, ((uint8_t *)&t8))) t16 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t15) + 3, ((uint8_t *)&t8))) t16 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t15) + 4, ((uint8_t *)&t8))) t16 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t15) + 5, ((uint8_t *)&t8))) t16 = 1;
        uint8_t t17 = (uint8_t)(t16);
        if (!t17) goto ie7;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t19 = (t18 > ((uint64_t)1ULL));
        if (!t19) goto ie7;
        {
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_slrfv83pestqp));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(30));
            gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix7;
ie7:;
ix7:;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_command_named_i67jehs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_command_named_i67jehs F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t9qkh0rsooc8d t2;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_so6kuvn2nvuue, 3) == 0)) goto mn4;
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
        t2 = t5;
        goto me3;
mn4:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s601j8au4fcin, 5) == 0)) goto mn6;
        gm_t9qkh0rsooc8d t7;
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
        t2 = t7;
        goto me3;
mn6:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_svua787vprkp6, 7) == 0)) goto mn8;
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)2ULL));
        t2 = t9;
        goto me3;
mn8:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_soduqpin16t7d, 5) == 0)) goto mn10;
        gm_t9qkh0rsooc8d t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)3ULL));
        t2 = t11;
        goto me3;
mn10:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_stjq3hbrk4g4r, 3) == 0)) goto mn12;
        gm_t9qkh0rsooc8d t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)4ULL));
        t2 = t13;
        goto me3;
mn12:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s6rsd5hkg3dmo, 5) == 0)) goto mn14;
        gm_t9qkh0rsooc8d t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)5ULL));
        t2 = t15;
        goto me3;
mn14:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sv1gci5lo7scg, 4) == 0)) goto mn16;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)6ULL));
        t2 = t17;
        goto me3;
mn16:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sh1emun6fmppt, 3) == 0)) goto mn18;
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)7ULL));
        t2 = t19;
        goto me3;
mn18:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_slllafvm0pbeb, 5) == 0)) goto mn20;
        gm_t9qkh0rsooc8d t21;
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)8ULL));
        t2 = t21;
        goto me3;
mn20:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sn2furlv6e7jl, 3) == 0)) goto mn22;
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)9ULL));
        t2 = t23;
        goto me3;
mn22:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s9r64t1166q1f, 4) == 0)) goto mn24;
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)10ULL));
        t2 = t25;
        goto me3;
mn24:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_srfagufvviad7, 5) == 0)) goto mn26;
        gm_t9qkh0rsooc8d t27;
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)11ULL));
        t2 = t27;
        goto me3;
mn26:;
        if (!(GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 0)) goto mn28;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sk5htm2k25gsj));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(15));
        gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn28:;
        gm_b16_8 t31 = gm_l0;
        gm_tnpifv20irnph t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
        gm_tabb710d9mq2e t33;
        gm_f_format_gl2aoqn((void *)&t33, (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_srcgs96ai7rj93j, 230, 26);
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn30:;
        gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 216, 12);
me3:;
        uint8_t t36 = 0;
        F.ret = t2;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_compiler_main_rh1a7t4(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_compiler_main_rh1a7t4 F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* command */
    GM_STACK_CHECK(&F, 854u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_node *pn2 = gm_find(cx, 621690230);
        if (!pn2) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 143, 13);
        gm_b16_8 t3;
        if (pn2->kind == GM_NATIVE) {
            size_t argn; const void *argp = gm_args(&argn);
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(argp));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(argn));
            t3 = t4;
        } else {
            t3 = ((gm_b16_8 (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l0 = t3;
    }
    {
        gm_b16_8 t5 = gm_l0;
        gm_b0_1 t6 = gm_f_check_arguments_are_text_r4m8jtj((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t8 = gm_l0;
        uint8_t t9 = gm_f_run_standalone_command_rnlbo5h((void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t9) goto ie7;
        {
            gm_b0_1 t11;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_b16_8 t13 = gm_l0;
        void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)));
        uint64_t t15 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t13) + 8);
        uint64_t t16 = (uintptr_t)((uint64_t)1ULL);
        gm_th2lqbo77pvsc t17;
        if (t16 < t15) {
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)t14 + t16 * 16));
        gm_th2lqbo77pvsc t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t19), t18);
        ((uint8_t *)&t19)[16] = 1;
            t17 = t19;
        } else {
        gm_th2lqbo77pvsc t20;
        memset(&t20, 0, sizeof t20);
            t17 = t20;
        }
        gm_b16_8 t21;
        if (((((uint8_t *)&t17))[16] != 0)) {
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 0));
            t21 = t22;
        } else {
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(0));
            t21 = t23;
        }
        gm_l1 = t21;
    }
    {
        memset((uint8_t *)&F.l2 + 5, 0, 3);
        memset((uint8_t *)&F.l2 + 35, 0, 5);
        memset((uint8_t *)&F.l2 + 58, 0, 6);
        memset((uint8_t *)&F.l2 + 90, 0, 6);
        memset((uint8_t *)&F.l2 + 122, 0, 6);
        memset((uint8_t *)&F.l2 + 178, 0, 6);
        memset((uint8_t *)&F.l2 + 209, 0, 7);
        gm_b16_8 t24 = gm_l1;
        gm_t9qkh0rsooc8d t25 = gm_f_command_named_i67jehs((void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l2 + 0, t25);
        gm_t9qkh0rsooc8d t26 = GM_LD(gm_t9qkh0rsooc8d, gm_c94f17fblj5);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l2 + 1, t26);
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 2, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 3, ((uint8_t)0));
        gm_t9qkh0rsooc8d t27 = GM_LD(gm_t9qkh0rsooc8d, gm_chtf2n20g83);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l2 + 4, t27);
        gm_th2lqbo77pvsc t28;
        memset(&t28, 0, sizeof t28);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 8, t28);
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 32, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 33, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 34, ((uint8_t)0));
        gm_b16_8 t29 = GM_LD(gm_b16_8, gm_cka88d5t8lu);
        GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 40, t29);
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 56, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 57, ((uint8_t)1));
        gm_th2lqbo77pvsc t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 64, t30);
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 88, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 89, ((uint8_t)0));
        GM_ST(uint64_t, (uint8_t *)&F.l2 + 96, ((uint64_t)1ULL));
        gm_th2bqc58ss467 t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(gm_th2bqc58ss467, (uint8_t *)&F.l2 + 104, t31);
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 120, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 121, ((uint8_t)0));
        gm_th2lqbo77pvsc t32;
        memset(&t32, 0, sizeof t32);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 128, t32);
        gm_th2lqbo77pvsc t33;
        memset(&t33, 0, sizeof t33);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 152, t33);
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 176, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 177, ((uint8_t)0));
        gm_th2lqbo77pvsc t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 184, t34);
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 208, ((uint8_t)0));
        gm_b16_8 t35 = GM_LD(gm_b16_8, gm_cka88d5t8lu_2);
        GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 216, t35);
        gm_b16_8 t36 = GM_LD(gm_b16_8, gm_cka88d5t8lu_3);
        GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 232, t36);
        gm_tctputthhrk9k t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(gm_tctputthhrk9k, (uint8_t *)&F.l2 + 248, t37);
        gm_th2lqbo77pvsc t38;
        memset(&t38, 0, sizeof t38);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 376, t38);
        gm_f_host_target_vsu78om((void *)&(*(gm_t0rfe1ou14h21 *)((uint8_t *)&F.l2 + 400)));
        gm_td6svc7gs468a t39;
        memset(&t39, 0, sizeof t39);
        GM_ST(gm_td6svc7gs468a, (uint8_t *)&F.l2 + 464, t39);
        gm_th2lqbo77pvsc t40;
        memset(&t40, 0, sizeof t40);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 520, t40);
        gm_f_options_2r71b7i((void *)&(*(gm_tm4vhr17lpr3p *)((uint8_t *)&F.l2 + 544)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t42 = ((GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0) + 0) == ((uint8_t)11ULL)));
        if (!t42) goto ie41;
        {
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 4), t44);
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_b16_8 t47 = gm_l0;
        void * t48 = (void *)((uint8_t *)&F.l2);
        void * t49 = (void *)((uint8_t *)&F.l3);
        uint8_t t50 = gm_f_read_options_44df0cc((void *)NULL, cx, NULL, t47, t48, t49);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t51 = (uint8_t)!t50;
        if (!t51) goto ie46;
        {
            gm_b16_8 t53 = gm_f_usage_8jc42fu((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_pfmaojsuds6a(cx, t53, gm_srcgs96ai7rj93j, 153, 9);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            gm_b0_1 t54;
            uint8_t t55 = 0;
            F.ret = t54;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t55;
            goto gm_ld1_1;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        uint8_t t57 = ((GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0) + 0) == ((uint8_t)6ULL)));
        if (!t57) goto ie56;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)&F.l2) + 544) + 1));
        if (!t58) goto ie56;
        {
            gm_b0_1 t60 = gm_f_list_rules_tjipqqd((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            gm_b0_1 t61;
            uint8_t t62 = 0;
            F.ret = t61;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t62;
            goto gm_ld1_1;
        }
        goto ix56;
ie56:;
ix56:;
    }
    {
        gm_t9qkh0rsooc8d t63 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        gm_b0_1 t66 = gm_f_check_input_files_sg6pbj1((void *)NULL, cx, NULL, t63, t65);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        gm_b0_1 t69 = gm_f_process_files_1slg8oc((void *)NULL, cx, NULL, t68, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t70 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_gvn47sj(t70);
    }
    goto gm_ls71;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t72 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_gvn47sj(t72);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls71:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entry_lp6pm8g(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_entry_lp6pm8g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t3mf58gv9kf3n t1 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_heap_allocator_gihki37());
    gm_t3mf58gv9kf3n t2 = t1;
    gm_node wn3 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t2, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
    gm_b0_1 t4 = gm_f_system_io_mcjcl9m();
    gm_b0_1 t5 = t4;
    gm_node wn6 = {.eff = 1018230276, .kind = GM_DECL, .clauses = gm_hd3g3ujqvah4, .env = (void *)&t5, .hctx = (&wn3), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn3)};
    gm_node wn7 = {.eff = 1579066750, .kind = GM_DECL, .clauses = gm_hdf4dspmusp5, .env = (void *)&t5, .hctx = (&wn3), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn6)};
    gm_node wn8 = {.eff = 1087599154, .kind = GM_DECL, .clauses = gm_hdvslis4o40u, .env = (void *)&t5, .hctx = (&wn3), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn7)};
    gm_node wn9 = {.eff = 2126215853, .kind = GM_DECL, .clauses = gm_hdu8v6n5i2qd, .env = (void *)&t5, .hctx = (&wn3), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn8)};
    gm_node wn10 = {.eff = 163658498, .kind = GM_DECL, .clauses = gm_hdffkeesmui4, .env = (void *)&t5, .hctx = (&wn3), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn9)};
    gm_node wn11 = {.eff = 2119230, .kind = GM_DECL, .clauses = gm_hdaqemfrshlp, .env = (void *)&t5, .hctx = (&wn3), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn10)};
    gm_b0_1 t14 = gm_f_main_6cov5v1((void *)NULL, (&wn11), NULL);
    if (gm_unw.kind) {
        goto gm_ll13;
    }
    F.w0 = t14;
    goto gm_ls15;
gm_ll13:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we12; }
    goto gm_lad;
gm_ls15:;
we12:;
    gm_b0_1 t16 = F.w0;
    F.ret = t16;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_help_topic_l4hcqu9(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_help_topic_l4hcqu9 F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* command */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 623u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
        uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
        uint64_t t5 = (uintptr_t)((uint64_t)1ULL);
        gm_th2lqbo77pvsc t6;
        if (t5 < t4) {
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)t3 + t5 * 16));
        gm_th2lqbo77pvsc t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[16] = 1;
            t6 = t8;
        } else {
        gm_th2lqbo77pvsc t9;
        memset(&t9, 0, sizeof t9);
            t6 = t9;
        }
        gm_b16_8 t10;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t10 = t11;
        } else {
            gm_th2lqbo77pvsc t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l1 = t10;
    }
    {
        uint8_t t15 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sa7tr9aaspmun, 4) == 0));
        if (!t15) goto ie14;
        {
            gm_b16_8 t17 = gm_l0;
            void * t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t17)));
            uint64_t t19 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t17) + 8);
            uint64_t t20 = (uintptr_t)((uint64_t)2ULL);
            gm_th2lqbo77pvsc t21;
            if (t20 < t19) {
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)t18 + t20 * 16));
            gm_th2lqbo77pvsc t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[16] = 1;
                t21 = t23;
            } else {
            gm_th2lqbo77pvsc t24;
            memset(&t24, 0, sizeof t24);
                t21 = t24;
            }
            uint8_t t25 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        {
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t29 = (uintptr_t)((uint64_t)2ULL);
            if (t29 > (uintptr_t)t27 || (uintptr_t)t27 > (uintptr_t)t27) gm_trap_slice(t29, (uintptr_t)t27, t27, gm_srcgs96ai7rj93j, 374, 14);
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t28 + t29 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)((uintptr_t)t27 - t29));
            gm_l2 = t30;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t32 = gm_l3;
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t34 = (t32 < t33);
                if (!t34) goto lb31;
                {
                    uint64_t t37 = gm_l3;
                    uint64_t t38 = (uintptr_t)t37;
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t38 * 16));
                    gm_l4 = t42;
                    {
                        gm_b16_8 t45 = gm_l4;
                        gm_t6j6smj7qoctu t46;
                        gm_b16_8 t47;
                        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s4chbmodjkinv));
                        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(6));
                        GM_ST(gm_b16_8, (uint8_t *)&t46 + 0, t47);
                        gm_b16_8 t48;
                        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s3a1oio07ri1p));
                        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(2));
                        GM_ST(gm_b16_8, (uint8_t *)&t46 + 16, t48);
                        uint8_t t49 = 0;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t46) + 0, ((uint8_t *)&t45))) t49 = 1;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t46) + 16, ((uint8_t *)&t45))) t49 = 1;
                        uint8_t t50 = (uint8_t)(t49);
                        if (!t50) goto ie44;
                        {
                            gm_b16_8 t52 = gm_l1;
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t52);
                            goto gm_fdone;
                        }
                        goto ix44;
ie44:;
ix44:;
                    }
                    goto me40;
mn41:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 374, 5);
me40:;
                }
lc31:;
                {
                    uint64_t t54 = gm_l3;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l3 = t57;
                }
            }
lb31:;
        }
    }
    {
        gm_th2lqbo77pvsc t58;
        memset(&t58, 0, sizeof t58);
        uint8_t t59 = 0;
        F.ret = t58;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_help_topic_l4hcqu9_c(gm_b16_8 a0) { gm_th2lqbo77pvsc gm_r; gm_f_help_topic_l4hcqu9(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_lint_or_fix_2e18k8s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_lint_or_fix_2e18k8s F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l2; /* std */
    gm_tqs5p4n2tq19v gm_l3; /* packages */
    GM_STACK_CHECK(&F, 170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_b16_8 t2 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t2;
    }
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 176));
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 177));
        gm_tqs5p4n2tq19v t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
        GM_ST(uint8_t, ((uint8_t *)&t5 + 1), t4);
        gm_l3 = t5;
    }
    {
        memset((uint8_t *)&F.l4 + 52, 0, 4);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 0, t6);
        gm_b16_8 t7 = gm_l2;
        gm_b16_8 t8 = gm_f_core_root_ip442nf((void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 16, t8);
        gm_t9qkh0rsooc8d t9 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 4));
        gm_b16_8 t10 = gm_f_mode_name_vpt4nrc(t9);
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 32, t10);
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 2));
        GM_ST(uint8_t, (uint8_t *)&F.l4 + 48, t11);
        gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 1));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l4 + 49, t12);
        gm_tqs5p4n2tq19v t13 = gm_l3;
        GM_ST(gm_tqs5p4n2tq19v, (uint8_t *)&F.l4 + 50, t13);
        memcpy((uint8_t *)&F.l4 + 56, (((uint8_t *)&F.l1) + 184), 24);
    }
    {
        uint8_t t15 = ((GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0) + 0) == ((uint8_t)7ULL)));
        if (!t15) goto ie14;
        {
            gm_b16_8 t17 = gm_l0;
            uint8_t t18 = gm_f_command_ugm7q4e((void *)NULL, cx, NULL, t17, &(*(gm_tiaut12ia9v85 *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        gm_b16_8 t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)&F.l1) + 544);
        uint8_t t22 = gm_f_command_o05p6ui((void *)NULL, cx, NULL, t20, t21, &(*(gm_tiaut12ia9v85 *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t23 = 0;
        F.ret = t22;
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
__attribute__((aligned(4))) uint8_t gm_f_load_program_4cvaa3s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_load_program_4cvaa3s F;
    gm_b16_8 gm_l0; /* input */
    uint32_t gm_l5; /* next_id */
    gm_b16_8 gm_l6; /* std */
    gm_b16_8 gm_l8; /* e */
    gm_b16_8 gm_l10; /* root */
    gm_b16_8 gm_l11; /* name */
    gm_b16_8 gm_l12; /* package_root */
    gm_b16_8 gm_l13; /* e */
    gm_b16_8 gm_l14; /* graph */
    uint8_t gm_l15; /* status */
    uint64_t gm_l17; /* m */
    uint8_t gm_l18; /* tests */
    uint8_t gm_l19; /* errors */
    GM_STACK_CHECK(&F, 1935u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_f_new_jr5ejs0((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_t3mf58gv9kf3n t4 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t3));
        gm_t3mf58gv9kf3n t5 = t4;
        gm_node wn6 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t5, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            gm_f_source_map_4v8dg4b((void *)&F.l3, (void *)NULL, (&wn6), NULL);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
        }
        {
            gm_f_diagnostics_n3kkhf4((void *)&F.l4, (void *)NULL, (&wn6), NULL);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
        }
        {
            gm_l5 = ((uint32_t)0ULL);
        }
        {
            gm_b16_8 t10 = gm_f_std_root_e0juohb((void *)NULL, (&wn6), NULL);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
            gm_l6 = t10;
        }
        {
            memmove(&F.l7, ((uint8_t *)&F.l1), sizeof F.l7);
        }
        {
            gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l7) + 0));
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)5ULL));
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)8ULL));
            gm_t9qkh0rsooc8d t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)9ULL));
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)10ULL));
            gm_t9qkh0rsooc8d t17;
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)11ULL));
            gm_t724v60om7sh5 t18;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 0), t13);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 1), t14);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 2), t15);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 3), t16);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 4), t17);
            uint8_t t19 = 0;
            if (gm_eq7nse29dnpe(((uint8_t *)&t18) + 0, ((uint8_t *)&t12))) t19 = 1;
            if (gm_eq7nse29dnpe(((uint8_t *)&t18) + 1, ((uint8_t *)&t12))) t19 = 1;
            if (gm_eq7nse29dnpe(((uint8_t *)&t18) + 2, ((uint8_t *)&t12))) t19 = 1;
            if (gm_eq7nse29dnpe(((uint8_t *)&t18) + 3, ((uint8_t *)&t12))) t19 = 1;
            if (gm_eq7nse29dnpe(((uint8_t *)&t18) + 4, ((uint8_t *)&t12))) t19 = 1;
            uint8_t t20 = (uint8_t)(t19);
            if (!t20) goto ie11;
            {
                void * t22 = (void *)((uint8_t *)&F.l7);
                gm_b0_1 t23 = gm_f_new_session_2d945g9((void *)NULL, (&wn6), NULL, t22);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
            {
                uint8_t t25 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)9ULL)));
                if (!t25) goto ie24;
                void * t26 = (void *)((uint8_t *)&F.l7);
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sn2furlv6e7jl));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(3));
                uint8_t t28 = gm_f_refuse_freestanding_j543r5e((void *)NULL, (&wn6), NULL, t26, t27);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                if (!t28) goto ie24;
                {
                    uint8_t t30 = 0;
                    F.ret = ((uint8_t)1ULL);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t30;
                    goto gm_ll8;
                }
                goto ix24;
ie24:;
ix24:;
            }
            {
                uint8_t t32 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)10ULL)));
                if (!t32) goto ie31;
                void * t33 = (void *)((uint8_t *)&F.l7);
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s9r64t1166q1f));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(4));
                uint8_t t35 = gm_f_refuse_freestanding_j543r5e((void *)NULL, (&wn6), NULL, t33, t34);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                if (!t35) goto ie31;
                {
                    uint8_t t37 = 0;
                    F.ret = ((uint8_t)1ULL);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t37;
                    goto gm_ll8;
                }
                goto ix31;
ie31:;
ix31:;
            }
            {
                uint8_t t39 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)11ULL)));
                if (!t39) goto ie38;
                void * t40 = (void *)((uint8_t *)&F.l7);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_srfagufvviad7));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(5));
                uint8_t t42 = gm_f_refuse_freestanding_j543r5e((void *)NULL, (&wn6), NULL, t40, t41);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                if (!t42) goto ie38;
                {
                    uint8_t t44 = 0;
                    F.ret = ((uint8_t)1ULL);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t44;
                    goto gm_ll8;
                }
                goto ix38;
ie38:;
ix38:;
            }
            {
                uint8_t t46 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)8ULL)));
                if (!t46) goto ie45;
                {
                    if (!(((((uint8_t *)&F.l7) + 520))[16] != 0)) goto ie48;
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 520));
                    gm_l8 = t49;
                    {
                        gm_b16_8 t51 = gm_l8;
                        if (gm_pf454qfrpt6r((&wn6), gm_s5lqhiu2bn6of, 7, gm_srcgs96ai7rj93j, 489, 21)) {
                            goto gm_ll8;
                        }
                        if ((GM_LD(uintptr_t, ((uint8_t *)&t51) + 8) != 0 && gm_pf454qfrpt6r((&wn6), GM_LD(void *, ((uint8_t *)&t51)), GM_LD(uintptr_t, ((uint8_t *)&t51) + 8), gm_srcgs96ai7rj93j, 489, 21))) {
                            goto gm_ll8;
                        }
                        if (gm_pf454qfrpt6r((&wn6), gm_so514oj5nbhcu, 1, gm_srcgs96ai7rj93j, 489, 21)) {
                            goto gm_ll8;
                        }
                    }
                    {
                        uint8_t t52 = 0;
                        F.ret = ((uint8_t)1ULL);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t52;
                        goto gm_ll8;
                    }
                    goto ix48;
ie48:;
ix48:;
                }
                goto ix45;
ie45:;
ix45:;
            }
            goto ix11;
ie11:;
ix11:;
        }
        {
            void * t53 = (void *)((uint8_t *)&F.l3);
            void * t54 = (void *)((uint8_t *)&F.l4);
            void * t55 = (void *)((uint8_t *)&gm_l5);
            gm_b16_8 t56 = gm_l6;
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 2));
            gm_f_loader_on0up0a((void *)&F.l9, (void *)NULL, (&wn6), NULL, t53, t54, t55, t56, t57);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
        }
        {
            gm_t0rfe1ou14h21 t58 = GM_LD(gm_t0rfe1ou14h21, (((uint8_t *)&F.l7) + 400));
            GM_ST(gm_t0rfe1ou14h21, (((uint8_t *)&F.l9) + 88), t58);
        }
        {
            gm_b16_8 t59 = gm_l0;
            gm_l10 = t59;
        }
        {
            gm_t9qkh0rsooc8d t61 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l7) + 0));
            gm_t9qkh0rsooc8d t62;
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)5ULL));
            gm_t9qkh0rsooc8d t63;
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)8ULL));
            gm_t9qkh0rsooc8d t64;
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)9ULL));
            gm_t9qkh0rsooc8d t65;
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)10ULL));
            gm_t9qkh0rsooc8d t66;
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)11ULL));
            gm_t724v60om7sh5 t67;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 0), t62);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 1), t63);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 2), t64);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 3), t65);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 4), t66);
            uint8_t t68 = 0;
            if (gm_eq7nse29dnpe(((uint8_t *)&t67) + 0, ((uint8_t *)&t61))) t68 = 1;
            if (gm_eq7nse29dnpe(((uint8_t *)&t67) + 1, ((uint8_t *)&t61))) t68 = 1;
            if (gm_eq7nse29dnpe(((uint8_t *)&t67) + 2, ((uint8_t *)&t61))) t68 = 1;
            if (gm_eq7nse29dnpe(((uint8_t *)&t67) + 3, ((uint8_t *)&t61))) t68 = 1;
            if (gm_eq7nse29dnpe(((uint8_t *)&t67) + 4, ((uint8_t *)&t61))) t68 = 1;
            uint8_t t69 = (uint8_t)(t68);
            if (!t69) goto ie60;
            {
                gm_b16_8 t71 = gm_l0;
                void * t72 = (void *)((uint8_t *)&F.l9);
                gm_tctputthhrk9k t73;
                gm_f_prepare_packages_tttjqji((void *)&t73, (void *)NULL, (&wn6), NULL, t71, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l7)), t72);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                gm_tr8dvlu9ik4gj t74;
                if (((((uint8_t *)&t73))[120] != 0)) {
                    t74 = (*(gm_tr8dvlu9ik4gj *)(((uint8_t *)&t73) + 0));
                } else {
                    uint8_t t75 = 0;
                    F.ret = ((uint8_t)1ULL);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t75;
                    goto gm_ll8;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tctputthhrk9k *)(((uint8_t *)&F.l7) + 248)) + 121, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tctputthhrk9k *)(((uint8_t *)&F.l7) + 248)) + 122, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tctputthhrk9k *)(((uint8_t *)&F.l7) + 248)) + 124, 0);
                ((uint8_t *)&(*(gm_tctputthhrk9k *)(((uint8_t *)&F.l7) + 248)))[120] = 1;
                GM_ST(gm_tr8dvlu9ik4gj, (uint8_t *)&(*(gm_tctputthhrk9k *)(((uint8_t *)&F.l7) + 248)) + 0, t74);
            }
            {
                uint8_t t77 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)8ULL)));
                if (!t77) goto ie76;
                uint8_t t78 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l7) + 400) + 24) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l7) + 400) + 24)), gm_soo4rtckloi4d, 12) == 0));
                if (!t78) goto ie76;
                gm_tctputthhrk9k t79 = GM_LD(gm_tctputthhrk9k, (((uint8_t *)&F.l7) + 248));
                gm_tr8dvlu9ik4gj t80;
                if (((((uint8_t *)&t79))[120] != 0)) {
                    t80 = (*(gm_tr8dvlu9ik4gj *)(((uint8_t *)&t79) + 0));
                } else {
                    gm_tr8dvlu9ik4gj t81;
                    gm_f_extras_joquqpb((void *)&t81, (void *)NULL, (&wn6), NULL);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                    t80 = t81;
                }
                void * t82 = (void *)((uint8_t *)&t80);
                uint8_t t83 = gm_f_is_empty_8l7niee(t82);
                uint8_t t84 = (uint8_t)!t83;
                if (!t84) goto ie76;
                {
                    if (gm_pf454qfrpt6r((&wn6), gm_som446v3b5hhj, 175, gm_srcgs96ai7rj93j, 500, 17)) {
                        goto gm_ll8;
                    }
                }
                {
                    uint8_t t86 = 0;
                    F.ret = ((uint8_t)1ULL);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t86;
                    goto gm_ll8;
                }
                goto ix76;
ie76:;
ix76:;
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            if (!(((((uint8_t *)&F.l7) + 152))[16] != 0)) goto ie87;
            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 152));
            gm_l11 = t88;
            {
                gm_b16_8 t90 = gm_l0;
                gm_th2lqbo77pvsc t91;
                gm_f_find_root_kjnm6hf((void *)&t91, (void *)NULL, (&wn6), NULL, t90);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                gm_b16_8 t92;
                if (((((uint8_t *)&t91))[16] != 0)) {
                    gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 0));
                    t92 = t93;
                } else {
                    {
                        if (gm_pf454qfrpt6r((&wn6), gm_srbaplcuib5iq, 88, gm_srcgs96ai7rj93j, 508, 17)) {
                            goto gm_ll8;
                        }
                    }
                    uint8_t t95 = 0;
                    F.ret = ((uint8_t)2ULL);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t95;
                    goto gm_ll8;
                }
                gm_l12 = t92;
            }
            {
                gm_b16_8 t96 = gm_l12;
                gm_b16_8 t97 = gm_l11;
                gm_t3ujhu6qph7de t98;
                gm_f_dependency_dir_grk9phj((void *)&t98, (void *)NULL, (&wn6), NULL, t96, &(*(gm_tv37d3dpt2opu *)(((uint8_t *)&F.l9) + 376)), t97);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                gm_b16_8 t99;
                if (!((((uint8_t *)&t98))[0] != 0)) {
                    gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
                    t99 = t100;
                } else {
                    gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
                    gm_l13 = t101;
                    {
                        gm_b16_8 t103 = gm_l13;
                        if (gm_pf454qfrpt6r((&wn6), gm_s5lqhiu2bn6of, 7, gm_srcgs96ai7rj93j, 512, 17)) {
                            goto gm_ll8;
                        }
                        if ((GM_LD(uintptr_t, ((uint8_t *)&t103) + 8) != 0 && gm_pf454qfrpt6r((&wn6), GM_LD(void *, ((uint8_t *)&t103)), GM_LD(uintptr_t, ((uint8_t *)&t103) + 8), gm_srcgs96ai7rj93j, 512, 17))) {
                            goto gm_ll8;
                        }
                        if (gm_pf454qfrpt6r((&wn6), gm_so514oj5nbhcu, 1, gm_srcgs96ai7rj93j, 512, 17)) {
                            goto gm_ll8;
                        }
                    }
                    uint8_t t104 = 0;
                    F.ret = ((uint8_t)2ULL);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t104;
                    goto gm_ll8;
                }
                gm_l10 = t99;
            }
            goto ix87;
ie87:;
ix87:;
        }
        {
            uint8_t t106 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)2ULL)));
            if (!t106) goto ie105;
            {
                void * t109 = (void *)((uint8_t *)&F.l9);
                gm_b16_8 t110 = gm_l10;
                gm_th2lqbo77pvsc t111;
                gm_f_load_modules_2vujekg((void *)&t111, (void *)NULL, (&wn6), NULL, t109, t110);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                if (!((((uint8_t *)&t111))[16] != 0)) goto ie108;
                gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                gm_l14 = t112;
                {
                    gm_b16_8 t114 = gm_l14;
                    gm_pfmaojsuds6a((&wn6), t114, gm_srcgs96ai7rj93j, 517, 48);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                goto ix108;
ie108:;
ix108:;
            }
            {
                void * t115 = (void *)((uint8_t *)&F.l3);
                void * t116 = (void *)((uint8_t *)&F.l4);
                gm_t9qkh0rsooc8d t117 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l7) + 1));
                gm_b0_1 t118 = gm_f_render_36ljhn0((void *)NULL, (&wn6), NULL, t115, t116, t117);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
            {
                uint8_t t119;
                void * t121 = (void *)((uint8_t *)&F.l4);
                uint8_t t122 = gm_f_has_errors_if7ai0m(t121);
                if (!t122) goto ie120;
                uint8_t t124 = ((uint8_t)1ULL);
                t119 = t124;
                goto ix120;
ie120:;
                uint8_t t126 = ((uint8_t)0ULL);
                t119 = t126;
ix120:;
                uint8_t t127 = 0;
                F.ret = t119;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t127;
                goto gm_ll8;
            }
            goto ix105;
ie105:;
ix105:;
        }
        {
            gm_l15 = ((uint8_t)0ULL);
        }
        {
            void * t129 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t130 = gm_l6;
            gm_b16_8 t131 = gm_f_core_root_ip442nf((void *)NULL, (&wn6), NULL, t130);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
            gm_t9qkh0rsooc8d t132 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l7) + 4));
            gm_b16_8 t133 = gm_f_mode_name_vpt4nrc(t132);
            uint8_t t134 = gm_f_load_core_du4go5i((void *)NULL, (&wn6), NULL, t129, t131, t133);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
            if (!t134) goto ie128;
            {
                void * t136 = (void *)((uint8_t *)&F.l9);
                gm_f_sema_r1nlnnm((void *)&F.l16, (void *)NULL, (&wn6), NULL, t136);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
            {
                void * t137 = (void *)((uint8_t *)&F.l16);
                gm_f_install_db2gf1l(t137);
            }
            {
                void * t138 = (void *)((uint8_t *)&F.l16);
                gm_b0_1 t139 = gm_f_resolve_ambient_6orl0vo((void *)NULL, (&wn6), NULL, t138);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
            {
                void * t141 = (void *)((uint8_t *)&F.l9);
                gm_b16_8 t142 = gm_l10;
                gm_tf5srh4mp4b3i t143 = gm_f_load_root_af4ud7b((void *)NULL, (&wn6), NULL, t141, t142);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                if (!((((uint8_t *)&t143))[8] != 0)) goto ie140;
                uint64_t t144 = GM_LD(uint64_t, ((uint8_t *)&t143));
                gm_l17 = t144;
                {
                    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 88));
                    if (!t147) goto ie146;
                    {
                        void * t149 = (void *)((uint8_t *)&F.l9);
                        uint64_t t150 = gm_l17;
                        gm_b0_1 t151 = gm_f_load_package_modules_t4hh6gf((void *)NULL, (&wn6), NULL, t149, t150);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    goto ix146;
ie146:;
ix146:;
                }
                {
                    void * t152 = (void *)((uint8_t *)&F.l16);
                    gm_b0_1 t153 = gm_f_resolve_declarations_vrpg1fl((void *)NULL, (&wn6), NULL, t152);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                {
                    uint8_t t155 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)3ULL)));
                    if (!t155) goto ie154;
                    {
                        void * t157 = (void *)((uint8_t *)&F.l16);
                        gm_b0_1 t158 = gm_f_check_consts_fsj0q4a((void *)NULL, (&wn6), NULL, t157);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        void * t159 = (void *)((uint8_t *)&F.l16);
                        gm_b16_8 t160 = gm_f_decl_dump_iah0p30((void *)NULL, (&wn6), NULL, t159);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_pfmaojsuds6a((&wn6), t160, gm_srcgs96ai7rj93j, 532, 21);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    goto ix154;
ie154:;
                    {
                        uint8_t t162 = 0;
                        gm_t9qkh0rsooc8d t163 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l7) + 0));
                        gm_t9qkh0rsooc8d t164;
                        GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)10ULL));
                        gm_t9qkh0rsooc8d t165;
                        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)11ULL));
                        gm_t0am46iohlpol t166;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t166 + 0), t164);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t166 + 1), t165);
                        uint8_t t167 = 0;
                        if (gm_eq7nse29dnpe(((uint8_t *)&t166) + 0, ((uint8_t *)&t163))) t167 = 1;
                        if (gm_eq7nse29dnpe(((uint8_t *)&t166) + 1, ((uint8_t *)&t163))) t167 = 1;
                        uint8_t t168 = (uint8_t)(t167);
                        if (!t168) {
                            uint8_t t169 = 0;
                            uint8_t t170 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)4ULL)));
                            if (t170) {
                                void * t171 = (void *)((uint8_t *)&F.l9);
                                uint64_t t172 = gm_l17;
                                gm_b16_8 t173;
                                GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sog4rksrmsqd2));
                                GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(4));
                                gm_tf5srh4mp4b3i t174 = gm_f_lookup_59juqoo(t171, t172, t173);
                                uint8_t t175;
                                if (((((uint8_t *)&t174))[8] != 0)) goto mn177;
                                t175 = ((uint8_t)1);
                                goto me176;
mn177:;
                                t175 = ((uint8_t)0);
                                goto me176;
mn178:;
                                gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 536, 119);
me176:;
                                t169 = t175;
                            } else t169 = 0;
                            t162 = t169;
                        } else t162 = 1;
                        gm_l18 = t162;
                    }
                    {
                        void * t179 = (void *)((uint8_t *)&F.l16);
                        uint64_t t180 = gm_l17;
                        uint8_t t181 = gm_l18;
                        gm_b0_1 t182 = gm_f_check_program_rltsqqp((void *)NULL, (&wn6), NULL, t179, t180, t181);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        void * t183 = (void *)((uint8_t *)&F.l4);
                        uint8_t t184 = gm_f_has_errors_if7ai0m(t183);
                        gm_l19 = t184;
                    }
                    {
                        void * t185 = (void *)((uint8_t *)&F.l3);
                        void * t186 = (void *)((uint8_t *)&F.l4);
                        gm_t9qkh0rsooc8d t187 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l7) + 1));
                        gm_b0_1 t188 = gm_f_render_36ljhn0((void *)NULL, (&wn6), NULL, t185, t186, t187);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        uint8_t t190 = gm_l19;
                        if (!t190) goto ie189;
                        {
                            uint8_t t192 = 0;
                            F.ret = ((uint8_t)1ULL);
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t192;
                            goto gm_ll8;
                        }
                        goto ix189;
ie189:;
ix189:;
                    }
                    {
                        uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                        switch (t200) { case ((uint8_t)0ULL): goto ma198; case ((uint8_t)1ULL): goto ma198; case ((uint8_t)2ULL): goto ma198; case ((uint8_t)3ULL): goto ma198; case ((uint8_t)4ULL): goto ma195; case ((uint8_t)5ULL): goto ma198; case ((uint8_t)6ULL): goto ma198; case ((uint8_t)7ULL): goto ma198; case ((uint8_t)8ULL): goto ma196; case ((uint8_t)9ULL): goto ma197; case ((uint8_t)10ULL): goto ma194; case ((uint8_t)11ULL): goto ma194; default: goto ma198; }
ma194:;
                        uint8_t t204 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                        if (t204 != ((uint8_t)10ULL)) goto pa203;
                        goto po202;
pa203:;
                        uint8_t t206 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                        if (t206 != ((uint8_t)11ULL)) goto pa205;
                        goto po202;
pa205:;
                        goto mn201;
po202:;
                        {
                            void * t208 = (void *)((uint8_t *)&F.l16);
                            gm_b16_8 t209 = gm_l10;
                            uint8_t t210 = gm_f_run_6mplkoc((void *)NULL, (&wn6), NULL, t208, t209, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l7)));
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_l15 = t210;
                        }
                        goto me193;
mn201:;
ma195:;
                        uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                        if (t212 != ((uint8_t)4ULL)) goto mn211;
                        void * t213 = (void *)((uint8_t *)&F.l16);
                        gm_b16_8 t214 = gm_f_tir_dump_ppa6e9g((void *)NULL, (&wn6), NULL, t213);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_pfmaojsuds6a((&wn6), t214, gm_srcgs96ai7rj93j, 544, 33);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me193;
mn211:;
ma196:;
                        uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                        if (t216 != ((uint8_t)8ULL)) goto mn215;
                        {
                            void * t219 = (void *)((uint8_t *)&F.l16);
                            uint64_t t220 = gm_l17;
                            gm_b16_8 t221 = gm_l10;
                            uint8_t t222 = gm_f_build_vkmrjt3((void *)NULL, (&wn6), NULL, t219, t220, t221, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l7)));
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            uint8_t t223 = (uint8_t)!t222;
                            if (!t223) goto ie218;
                            {
                                gm_l15 = ((uint8_t)1ULL);
                            }
                            goto ix218;
ie218:;
ix218:;
                        }
                        goto me193;
mn215:;
ma197:;
                        uint8_t t226 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                        if (t226 != ((uint8_t)9ULL)) goto mn225;
                        {
                            uint8_t t228;
                            uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 56));
                            if (!t230) goto ie229;
                            void * t232 = (void *)((uint8_t *)&F.l16);
                            uint64_t t233 = gm_l17;
                            gm_b16_8 t234 = gm_l10;
                            uint8_t t235 = gm_f_run_interp_t6l3qao((void *)NULL, (&wn6), NULL, t232, t233, t234, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l7)));
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            uint8_t t236 = t235;
                            t228 = t236;
                            goto ix229;
ie229:;
                            void * t238 = (void *)((uint8_t *)&F.l16);
                            uint64_t t239 = gm_l17;
                            uint8_t t240 = gm_f_run_dg4eogs((void *)NULL, (&wn6), NULL, t238, t239, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l7)));
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            uint8_t t241 = t240;
                            t228 = t241;
ix229:;
                            gm_l15 = t228;
                        }
                        goto me193;
mn225:;
ma198:;
                        goto me193;
mn242:;
ma199:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 542, 21);
me193:;
                    }
ix154:;
                }
                goto ix140;
ie140:;
ix140:;
            }
            goto ix128;
ie128:;
ix128:;
        }
        {
            void * t245 = (void *)((uint8_t *)&F.l4);
            uint8_t t246 = gm_f_has_errors_if7ai0m(t245);
            if (!t246) goto ie244;
            uint8_t t247 = gm_l15;
            uint8_t t248 = ((t247 == ((uint8_t)0ULL)));
            if (!t248) goto ie244;
            {
                gm_l15 = ((uint8_t)1ULL);
            }
            goto ix244;
ie244:;
ix244:;
        }
        {
            void * t250 = (void *)((uint8_t *)&F.l3);
            void * t251 = (void *)((uint8_t *)&F.l4);
            gm_t9qkh0rsooc8d t252 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l7) + 1));
            gm_b0_1 t253 = gm_f_render_36ljhn0((void *)NULL, (&wn6), NULL, t250, t251, t252);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
        }
        {
            uint8_t t254 = gm_l15;
            uint8_t t255 = 0;
            F.ret = t254;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t255;
            goto gm_ll8;
        }
        goto gm_ls256;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_ld1_1;
gm_ls256:;
we7:;
    }
    {
        void * t257 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t257);
    }
    goto gm_ls258;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t259 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t259);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls258:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_main_6cov5v1(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_main_6cov5v1 F;
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint64_t t2 = GM_LD(uint64_t, gm_cjt60aq4qvt);
        void * t3 = gm_x_calloc_vohseaa(((uint64_t)1ULL), t2);
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            {
                gm_b0_1 t7 = gm_f_compiler_main_rh1a7t4((void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        F.l0 = t4;
    }
    {
    }
    {
        gm_f_scheduler_p4n24jh((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        gm_with gw12; memset(&gw12, 0, sizeof gw12);
        void * t13 = (void *)((uint8_t *)&F.l1);
        void * t14 = t13;
        gm_node wn15 = {.eff = 1597678050, .kind = GM_DECL, .clauses = gm_hd3ip07hju0a, .env = (void *)&t14, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = &gw12, .next = cx};
        void * t16 = F.l0;
        void * t17 = ((uint8_t *)t16);
        uint64_t t18 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t19 = GM_LD(uint64_t, gm_cjt60aq4qvt);
        uint64_t t20 = (uintptr_t)t19;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t17 + t18 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(t20 - t18));
        gw12.body_cx = (&wn15);
        gm_fiber *fb27 = gm_fiber_new(&gw12, (uint8_t *)GM_LD(void *, ((uint8_t *)&t21)), GM_LD(uintptr_t, ((uint8_t *)&t21) + 8), gm_wb_main_6cov5v1_0, 0, gm_srcgs96ai7rj93j, 136, 5);
        fb27->frame = &F; fb27->env = env; fb27->inst = inst; fb27->cx = (&wn15);
        gm_with_go(&gw12, fb27);
        for (;;) {
            if (gw12.event == GM_EV_PERFORM) {
                gm_with_clause(&gw12);
                if (gm_unw.kind == GM_RESUME) { gm_unw.kind = GM_NONE; gm_with_go(&gw12, gw12.next); continue; }
                gm_with_end(&gw12, !(gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0), gm_srcgs96ai7rj93j, 136, 5);
            } else if (gw12.event_fiber == fb27 && !gm_unw.kind) {
                gm_with_end(&gw12, false, gm_srcgs96ai7rj93j, 136, 5);
                goto we28;
            } else {
                if (!gm_unw.kind) gm_trap("a fiber's function returned", gm_srcgs96ai7rj93j, 136, 5);
                gm_with_end(&gw12, true, gm_srcgs96ai7rj93j, 136, 5);
            }
            if (!gm_unw.kind) gm_trap("internal error: a `ctl` clause finished normally", gm_srcgs96ai7rj93j, 136, 5);
            goto gm_ll29;
gm_ll29:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we28; }
            goto gm_ld1_2;
        }
we28:;
        gm_b0_1 t30 = F.w0;
    }
    {
        void * t31 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_ab943ap(t31);
    }
    {
        void * t32 = F.l0;
        gm_x_free_vdscb6h(t32);
    }
    goto gm_ls33;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_ab943ap(t34);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t35 = F.l0;
        gm_x_free_vdscb6h(t35);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls33:;
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
inline __attribute__((aligned(4))) uint64_t gm_f_parse_jobs_e7r1uqv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_jobs_e7r1uqv F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 359u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sfdbfrv46lihd));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        uint8_t t5 = gm_f_contains_gkki4kq(t3, t4);
        if (!t5) goto ie2;
        {
            gm_b16_8 t7 = gm_l0;
            gm_tnpifv20irnph t8;
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
            gm_tabb710d9mq2e t9;
            gm_f_format_l7u3cq2((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srcgs96ai7rj93j, 343, 46);
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
            gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t12 = gm_l0;
        gm_tutnubt6u4hoq t13;
        gm_f_parse_int_3ce02np((void *)&t13, t12);
        uint64_t t14;
        if (!((((uint8_t *)&t13))[0] != 0)) {
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&t13) + 8));
            t14 = t15;
        } else {
            gm_b16_8 t16 = gm_l0;
            gm_tnpifv20irnph t17;
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
            gm_tabb710d9mq2e t18;
            gm_f_format_l7u3cq2((void *)&t18, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcgs96ai7rj93j, 344, 54);
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_l1 = t14;
    }
    {
        uint64_t t21;
        uint64_t t23 = gm_l1;
        uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
        if (!t24) goto ie22;
        uint32_t t26 = gm_f_cpu_count_0o5882g();
        uint64_t t27 = (uint64_t)t26;
        uint64_t t28 = t27;
        t21 = t28;
        goto ix22;
ie22:;
        uint64_t t30 = gm_l1;
        uint64_t t31 = t30;
        t21 = t31;
ix22:;
        uint8_t t32 = 0;
        F.ret = t21;
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
inline __attribute__((aligned(4))) double gm_f_parse_timeout_4hlkio8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_timeout_4hlkio8 F;
    gm_b16_8 gm_l0; /* text */
    double gm_l1; /* seconds */
    GM_STACK_CHECK(&F, 268u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        double t2;
        gm_b16_8 t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sfdbfrv46lihd));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
        uint8_t t6 = gm_f_contains_gkki4kq(t4, t5);
        if (!t6) goto ie3;
        double t8 = ((double)0e0);
        t2 = t8;
        goto ix3;
ie3:;
        gm_b16_8 t10 = gm_l0;
        gm_tspt3vn9ljcnd t11;
        gm_f_parse_float_17hgksa((void *)&t11, t10);
        double t12;
        if (!((((uint8_t *)&t11))[0] != 0)) {
            double t13 = GM_LD(double, (((uint8_t *)&t11) + 8));
            t12 = t13;
        } else {
            t12 = ((double)0e0);
        }
        double t14 = t12;
        t2 = t14;
ix3:;
        gm_l1 = t2;
    }
    {
        double t16 = gm_l1;
        uint8_t t17 = (t16 > ((double)0e0));
        if (!t17) goto ie15;
        double t18 = gm_l1;
        uint8_t t19 = (t18 < ((double)1e9));
        if (!t19) goto ie15;
        {
            double t21 = gm_l1;
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        gm_b16_8 t23 = gm_l0;
        gm_tnpifv20irnph t24;
        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
        gm_tabb710d9mq2e t25;
        gm_f_format_tt07921((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcgs96ai7rj93j, 352, 17);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
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
inline __attribute__((aligned(4))) void gm_f_prepare_packages_tttjqji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, void * a2) {
    struct gm_fr_prepare_packages_tttjqji F;
    gm_b16_8 gm_l0; /* input */
    void * gm_l2; /* l */
    GM_STACK_CHECK(&F, 581u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 176));
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 177));
        gm_tqs5p4n2tq19v t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
        GM_ST(uint8_t, ((uint8_t *)&t5 + 1), t4);
        gm_tdsi8a7qahakk t6;
        gm_f_prepare_build_b1lg1n1((void *)&t6, (void *)NULL, cx, NULL, t2, t5, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l1) + 184)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tljis7hgku5bj t7;
        if (((((uint8_t *)&t6))[168] != 0)) {
            t7 = (*(gm_tljis7hgku5bj *)(((uint8_t *)&t6) + 0));
        } else {
            gm_tctputthhrk9k t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        F.l3 = t7;
    }
    {
        void * t10 = gm_l2;
        gm_tv37d3dpt2opu t11 = GM_LD(gm_tv37d3dpt2opu, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_tv37d3dpt2opu, (((uint8_t *)t10) + 376), t11);
    }
    {
        const uint8_t *t12 = (((uint8_t *)&F.l3) + 48);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tctputthhrk9k *)gm_out) + 121, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tctputthhrk9k *)gm_out) + 122, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tctputthhrk9k *)gm_out) + 124, 0);
        ((uint8_t *)&(*(gm_tctputthhrk9k *)gm_out))[120] = 1;
        memmove((uint8_t *)&(*(gm_tctputthhrk9k *)gm_out) + 0, t12, 120);
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
__attribute__((aligned(4))) gm_t9t0nevf2k2cl gm_f_process_file_igvmu0h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_process_file_igvmu0h F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l3; /* e */
    gm_b16_8 gm_l4; /* text */
    uint32_t gm_l7; /* id */
    uint32_t gm_l11; /* next_id */
    gm_t08olq97rscn3 gm_l12; /* tree */
    GM_STACK_CHECK(&F, 1524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memmove(&F.l2, ((uint8_t *)a1), sizeof F.l2);
    }
    {
        gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 0));
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)5ULL));
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)8ULL));
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)9ULL));
        gm_t9qkh0rsooc8d t7;
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)10ULL));
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)11ULL));
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)6ULL));
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)7ULL));
        gm_t6ofeqo2h242p t11;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 0), t4);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 1), t5);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 2), t6);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 3), t7);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 4), t8);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 5), t9);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 6), t10);
        uint8_t t12 = 0;
        if (gm_eq7nse29dnpe(((uint8_t *)&t11) + 0, ((uint8_t *)&t3))) t12 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t11) + 1, ((uint8_t *)&t3))) t12 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t11) + 2, ((uint8_t *)&t3))) t12 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t11) + 3, ((uint8_t *)&t3))) t12 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t11) + 4, ((uint8_t *)&t3))) t12 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t11) + 5, ((uint8_t *)&t3))) t12 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t11) + 6, ((uint8_t *)&t3))) t12 = 1;
        uint8_t t13 = (uint8_t)(t12);
        if (!t13) goto ie2;
        {
            gm_b16_8 t15 = gm_l0;
            void * t16 = (void *)((uint8_t *)&F.l2);
            gm_tl572d7579hns t17;
            gm_f_apply_build_settings_bc3dtmk((void *)&t17, (void *)NULL, cx, NULL, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t18;
            if (!((((uint8_t *)&t17))[0] != 0)) {
                gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 8));
                t18 = t19;
            } else {
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
                gm_l3 = t20;
                {
                    gm_b16_8 t22 = gm_l3;
                    if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcgs96ai7rj93j, 392, 13)) {
                        goto gm_lad;
                    }
                    if ((GM_LD(uintptr_t, ((uint8_t *)&t22) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t22)), GM_LD(uintptr_t, ((uint8_t *)&t22) + 8), gm_srcgs96ai7rj93j, 392, 13))) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcgs96ai7rj93j, 392, 13)) {
                        goto gm_lad;
                    }
                }
                gm_t9t0nevf2k2cl t23;
                GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
                GM_ST(uint64_t, (uint8_t *)&t23 + 8, 0);
                ((uint8_t *)&t23)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t23 + 4), ((uint8_t)1ULL));
                uint8_t t24 = ((((uint8_t *)&t23))[0] != 0);
                F.ret = t23;
                goto gm_fret;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t9qkh0rsooc8d t26 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 0));
        gm_t9qkh0rsooc8d t27;
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)6ULL));
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)7ULL));
        gm_t0am46iohlpol t29;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 0), t27);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 1), t28);
        uint8_t t30 = 0;
        if (gm_eq7nse29dnpe(((uint8_t *)&t29) + 0, ((uint8_t *)&t26))) t30 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t29) + 1, ((uint8_t *)&t26))) t30 = 1;
        uint8_t t31 = (uint8_t)(t30);
        if (!t31) goto ie25;
        {
            gm_b16_8 t33 = gm_l0;
            uint8_t t34 = gm_f_lint_or_fix_2e18k8s((void *)NULL, cx, NULL, t33, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9t0nevf2k2cl t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 6, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 8, 0);
            ((uint8_t *)&t35)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t35 + 4), t34);
            uint8_t t36 = ((((uint8_t *)&t35))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 0));
        gm_t9qkh0rsooc8d t39;
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0ULL));
        gm_t9qkh0rsooc8d t40;
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)1ULL));
        gm_t0am46iohlpol t41;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 0), t39);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 1), t40);
        uint8_t t42 = 0;
        if (gm_eq7nse29dnpe(((uint8_t *)&t41) + 0, ((uint8_t *)&t38))) t42 = 1;
        if (gm_eq7nse29dnpe(((uint8_t *)&t41) + 1, ((uint8_t *)&t38))) t42 = 1;
        uint8_t t43 = (uint8_t)(!t42);
        if (!t43) goto ie37;
        {
            gm_b16_8 t45 = gm_l0;
            uint8_t t46 = gm_f_load_program_4cvaa3s((void *)NULL, cx, NULL, t45, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9t0nevf2k2cl t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint8_t, (uint8_t *)&t47 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 6, 0);
            GM_ST(uint64_t, (uint8_t *)&t47 + 8, 0);
            ((uint8_t *)&t47)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t47 + 4), t46);
            uint8_t t48 = ((((uint8_t *)&t47))[0] != 0);
            F.ret = t47;
            goto gm_fret;
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        gm_b16_8 t49 = gm_l0;
        gm_tgvh15qshf643 t50;
        gm_f_read_all_r9rh9qd((void *)&t50, (void *)NULL, cx, NULL, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t50))[0] != 0)) {
            gm_tj5jn79kq9pnm t51 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t50) + 8));
            gm_t9t0nevf2k2cl t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            ((uint8_t *)&t52)[0] = 1;
            GM_ST(gm_tj5jn79kq9pnm, ((uint8_t *)&t52 + 4), t51);
            F.ret = t52;
            goto gm_fret;
        }
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
        gm_l4 = t53;
    }
    {
    }
    {
        gm_f_new_jr5ejs0((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        void * t58 = (void *)((uint8_t *)&F.l5);
        gm_t3mf58gv9kf3n t59 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t58));
        gm_t3mf58gv9kf3n t60 = t59;
        gm_node wn61 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t60, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            gm_f_source_map_4v8dg4b((void *)&F.l6, (void *)NULL, (&wn61), NULL);
            if (gm_unw.kind) {
                goto gm_ll63;
            }
        }
        {
            void * t65 = (void *)((uint8_t *)&F.l6);
            gm_b16_8 t66 = gm_l0;
            gm_b16_8 t67 = gm_l4;
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
            uint32_t t69 = gm_f_add_pva1tcq((void *)NULL, (&wn61), NULL, t65, t66, t68);
            if (gm_unw.kind) {
                goto gm_ll63;
            }
            gm_l7 = t69;
        }
        {
            gm_f_diagnostics_n3kkhf4((void *)&F.l8, (void *)NULL, (&wn61), NULL);
            if (gm_unw.kind) {
                goto gm_ll63;
            }
        }
        {
            gm_tr8dvlu9ik4gj t70;
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 2));
            if (!t72) goto ie71;
            uint32_t t74 = gm_l7;
            gm_b16_8 t75 = gm_l4;
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
            void * t77 = (void *)((uint8_t *)&F.l8);
            gm_tr8dvlu9ik4gj t78;
            gm_f_lex_recovering_obo2892((void *)&t78, (void *)NULL, (&wn61), NULL, t74, t76, t77);
            if (gm_unw.kind) {
                goto gm_ll63;
            }
            gm_tr8dvlu9ik4gj t79 = t78;
            t70 = t79;
            goto ix71;
ie71:;
            uint32_t t81 = gm_l7;
            gm_b16_8 t82 = gm_l4;
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
            void * t84 = (void *)((uint8_t *)&F.l8);
            gm_tr8dvlu9ik4gj t85;
            gm_f_lex_dc8mbmf((void *)&t85, (void *)NULL, (&wn61), NULL, t81, t83, t84);
            if (gm_unw.kind) {
                goto gm_ll63;
            }
            gm_tr8dvlu9ik4gj t86 = t85;
            t70 = t86;
ix71:;
            F.l9 = t70;
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l10, (void *)NULL, (&wn61), NULL);
            if (gm_unw.kind) {
                goto gm_ll63;
            }
        }
        {
            uint8_t t88 = ((GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0) + 0) == ((uint8_t)0ULL)));
            if (!t88) goto ie87;
            {
                void * t90 = (void *)((uint8_t *)&F.l6);
                uint32_t t91 = gm_l7;
                void * t92 = gm_f_file_o15fqku(t90, t91);
                gm_b16_8 t93 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l9) + 0) + 0));
                gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                void * t95 = (void *)((uint8_t *)&F.l10);
                gm_b0_1 t96 = gm_f_dump_tokens_4c95gl6((void *)NULL, (&wn61), NULL, t92, t94, t95);
                if (gm_unw.kind) {
                    goto gm_ll63;
                }
            }
            goto ix87;
ie87:;
            {
                gm_l11 = ((uint32_t)0ULL);
            }
            {
                uint32_t t98 = gm_l7;
                void * t99 = (void *)((uint8_t *)&F.l9);
                void * t100 = (void *)((uint8_t *)&gm_l11);
                void * t101 = (void *)((uint8_t *)&F.l8);
                gm_f_parse_file_70g7vok((void *)&gm_l12, (void *)NULL, (&wn61), NULL, t98, t99, t100, t101);
                if (gm_unw.kind) {
                    goto gm_ll63;
                }
            }
            {
                uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 3));
                if (!t103) goto ie102;
                {
                    void * t105 = (void *)((uint8_t *)&gm_l12);
                    void * t106 = (void *)((uint8_t *)&F.l10);
                    gm_b0_1 t107 = gm_f_dump_file_cno3fos((void *)NULL, (&wn61), NULL, t105, t106);
                    if (gm_unw.kind) {
                        goto gm_ll63;
                    }
                }
                goto ix102;
ie102:;
ix102:;
            }
ix87:;
        }
        {
            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
            gm_pfmaojsuds6a((&wn61), t109, gm_srcgs96ai7rj93j, 415, 9);
            if (gm_unw.kind) {
                goto gm_ll63;
            }
        }
        {
            void * t110 = (void *)((uint8_t *)&F.l6);
            void * t111 = (void *)((uint8_t *)&F.l8);
            gm_t9qkh0rsooc8d t112 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 1));
            gm_b0_1 t113 = gm_f_render_36ljhn0((void *)NULL, (&wn61), NULL, t110, t111, t112);
            if (gm_unw.kind) {
                goto gm_ll63;
            }
        }
        {
            gm_t9t0nevf2k2cl t114;
            void * t116 = (void *)((uint8_t *)&F.l8);
            uint8_t t117 = gm_f_has_errors_if7ai0m(t116);
            if (!t117) goto ie115;
            uint8_t t119 = ((uint8_t)1ULL);
            gm_t9t0nevf2k2cl t120;
            GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
            GM_ST(uint8_t, (uint8_t *)&t120 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t120 + 6, 0);
            GM_ST(uint64_t, (uint8_t *)&t120 + 8, 0);
            ((uint8_t *)&t120)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t120 + 4), t119);
            t114 = t120;
            goto ix115;
ie115:;
            uint8_t t122 = ((uint8_t)0ULL);
            gm_t9t0nevf2k2cl t123;
            GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
            GM_ST(uint8_t, (uint8_t *)&t123 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t123 + 6, 0);
            GM_ST(uint64_t, (uint8_t *)&t123 + 8, 0);
            ((uint8_t *)&t123)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t123 + 4), t122);
            t114 = t123;
ix115:;
            uint8_t t124 = ((((uint8_t *)&t114))[0] != 0);
            F.ret = t114;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t124;
            goto gm_ll63;
        }
        goto gm_ls125;
gm_ll63:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we62; }
        goto gm_ld1_2;
gm_ls125:;
we62:;
    }
    {
        void * t126 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_sejdpqv(t126);
    }
    {
        gm_b16_8 t127 = gm_l4;
        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
        gm_b0_1 t129 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t128, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k26b4lfjb04)));
    }
    goto gm_ls130;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t131 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_sejdpqv(t131);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b16_8 t132 = gm_l4;
        gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
        gm_b0_1 t134 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t133, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k26b4lfjb04)));
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls130:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_process_files_1slg8oc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_process_files_1slg8oc F;
    gm_b16_8 gm_l0; /* files */
    uint8_t gm_l2; /* status */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* path */
    uint8_t gm_l6; /* code */
    GM_STACK_CHECK(&F, 243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_l2 = ((uint8_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 16));
                    gm_l5 = t15;
                    {
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t19 = (t18 > ((uint64_t)1ULL));
                        if (!t19) goto ie17;
                        {
                            gm_b16_8 t21 = gm_l5;
                            if (gm_pf1m4b8q4e78(cx, gm_s07iqjq52hems, 4, gm_srcgs96ai7rj93j, 325, 13)) {
                                goto gm_ll8;
                            }
                            if ((GM_LD(uintptr_t, ((uint8_t *)&t21) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t21)), GM_LD(uintptr_t, ((uint8_t *)&t21) + 8), gm_srcgs96ai7rj93j, 325, 13))) {
                                goto gm_ll8;
                            }
                            if (gm_pf1m4b8q4e78(cx, gm_s5gaofiqkqtj6, 5, gm_srcgs96ai7rj93j, 325, 13)) {
                                goto gm_ll8;
                            }
                        }
                        {
                            gm_b16_8 t22 = gm_l5;
                            if (gm_pf454qfrpt6r(cx, gm_s07iqjq52hems, 4, gm_srcgs96ai7rj93j, 326, 13)) {
                                goto gm_ll8;
                            }
                            if ((GM_LD(uintptr_t, ((uint8_t *)&t22) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t22)), GM_LD(uintptr_t, ((uint8_t *)&t22) + 8), gm_srcgs96ai7rj93j, 326, 13))) {
                                goto gm_ll8;
                            }
                            if (gm_pf454qfrpt6r(cx, gm_s5gaofiqkqtj6, 5, gm_srcgs96ai7rj93j, 326, 13)) {
                                goto gm_ll8;
                            }
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        gm_b16_8 t23 = gm_l5;
                        gm_t9t0nevf2k2cl t24 = gm_f_process_file_igvmu0h((void *)NULL, cx, NULL, t23, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l1)));
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (!((((uint8_t *)&t24))[0] != 0)) goto mn26;
                        {
                            gm_b16_8 t28 = gm_l5;
                            if (gm_pf454qfrpt6r(cx, gm_sqefppq1so38u, 20, gm_srcgs96ai7rj93j, 330, 17)) {
                                goto gm_ll8;
                            }
                            if ((GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t28)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8), gm_srcgs96ai7rj93j, 330, 17))) {
                                goto gm_ll8;
                            }
                            if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcgs96ai7rj93j, 330, 17)) {
                                goto gm_ll8;
                            }
                        }
                        {
                            gm_l2 = ((uint8_t)2ULL);
                        }
                        goto me25;
mn26:;
                        if (((((uint8_t *)&t24))[0] != 0)) goto mn29;
                        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t24) + 4));
                        gm_l6 = t30;
                        {
                            uint8_t t33 = gm_l2;
                            uint8_t t34 = ((t33 == ((uint8_t)0ULL)));
                            if (!t34) goto ie32;
                            {
                                uint8_t t36 = gm_l6;
                                gm_l2 = t36;
                            }
                            goto ix32;
ie32:;
ix32:;
                        }
                        goto me25;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 328, 9);
me25:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 323, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t38 = gm_l4;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l4 = t41;
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
        uint8_t t43 = gm_l2;
        uint8_t t44 = (!(t43 == ((uint8_t)0ULL)));
        if (!t44) goto ie42;
        {
            uint8_t t46 = gm_l2;
            gm_node *pn47 = gm_find(cx, 621690230);
            if (!pn47) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 338, 26);
            if (pn47->kind == GM_NATIVE) {
                gm_unw.code = (uint8_t)t46;
                gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_lad;
            } else {
                ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn47->clauses[2])(pn47->env, pn47->hctx, pn47, t46);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix42;
ie42:;
ix42:;
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
__attribute__((aligned(4))) uint8_t gm_f_read_options_44df0cc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, void * a2) {
    struct gm_fr_read_options_44df0cc F;
    gm_b16_8 gm_l0; /* args */
    void * gm_l1; /* opts */
    void * gm_l2; /* files */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l4; /* a */
    gm_b16_8 gm_l6; /* message */
    gm_b16_8 gm_l7; /* message */
    GM_STACK_CHECK(&F, 701u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint64_t)2ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l3;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l3;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srcgs96ai7rj93j, 239, 14);
                gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 16));
                gm_l4 = t11;
            }
            {
                void * t13 = gm_l1;
                uint8_t t14 = ((GM_LD(uint8_t, (((uint8_t *)t13) + 0) + 0) == ((uint8_t)9ULL)));
                if (!t14) goto ie12;
                void * t15 = gm_l2;
                uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
                uint8_t t17 = (t16 > ((uint64_t)0ULL));
                if (!t17) goto ie12;
                {
                    void * t19 = gm_l1;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t21 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t22 = gm_l3;
                    uint64_t t23 = (uintptr_t)t22;
                    if (t23 > (uintptr_t)t20 || (uintptr_t)t20 > (uintptr_t)t20) gm_trap_slice(t23, (uintptr_t)t20, t20, gm_srcgs96ai7rj93j, 242, 33);
                    gm_b16_8 t24;
                    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t21 + t23 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)((uintptr_t)t20 - t23));
                    GM_ST(gm_b16_8, (((uint8_t *)t19) + 40), t24);
                }
                {
                    goto lb2;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                gm_b16_8 t26 = gm_l4;
                gm_t6j6smj7qoctu t27;
                gm_b16_8 t28;
                GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s4chbmodjkinv));
                GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&t27 + 0, t28);
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s3a1oio07ri1p));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
                GM_ST(gm_b16_8, (uint8_t *)&t27 + 16, t29);
                uint8_t t30 = 0;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t27) + 0, ((uint8_t *)&t26))) t30 = 1;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t27) + 16, ((uint8_t *)&t26))) t30 = 1;
                uint8_t t31 = (uint8_t)(t30);
                if (!t31) goto ie25;
                {
                    uint8_t t33 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                gm_b16_8 t34 = gm_l0;
                uint64_t t35 = gm_l3;
                gm_tepv5jklv667k t36;
                gm_f_scan_ll1gqkn((void *)&t36, (void *)NULL, cx, NULL, t34, t35, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                switch (t42) { case ((uint8_t)0ULL): goto ma38; case ((uint8_t)1ULL): goto ma40; case ((uint8_t)2ULL): goto ma39; default: goto ma41; }
ma38:;
                uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                if (t44 != ((uint8_t)0ULL)) goto mn43;
                F.l5 = (*(gm_t04fuf8kbk07t *)(((uint8_t *)&t36) + 8));
                {
                    void * t46 = gm_l1;
                    gm_b0_1 t47 = gm_f_apply_option_uqodud5((void *)NULL, cx, NULL, t46, &(*(gm_t04fuf8kbk07t *)((uint8_t *)&F.l5)));
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                }
                {
                    uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 32));
                    gm_l3 = t48;
                }
                {
                    goto lc2;
                }
                goto me37;
mn43:;
ma39:;
                uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                if (t50 != ((uint8_t)2ULL)) goto mn49;
                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
                gm_l6 = t51;
                gm_b16_8 t52 = gm_l6;
                gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t52);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
                goto me37;
mn49:;
ma40:;
                uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                if (t54 != ((uint8_t)1ULL)) goto mn53;
                goto me37;
mn53:;
ma41:;
                gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 246, 9);
me37:;
            }
            {
                uint64_t t56 = gm_l3;
                uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                if (o58) gm_trap("integer overflow in `+`", gm_srcgs96ai7rj93j, 255, 9);
                uint64_t t59 = r57;
                gm_l3 = t59;
            }
            {
                void * t61 = gm_l1;
                uint8_t t62 = ((GM_LD(uint8_t, (((uint8_t *)t61) + 0) + 0) == ((uint8_t)6ULL)));
                if (!t62) goto ie60;
                {
                    void * t64 = gm_l1;
                    void * t65 = (void *)(((uint8_t *)t64) + 544);
                    gm_b16_8 t66 = gm_l4;
                    gm_tl572d7579hns t67;
                    gm_f_parse_arg_3o22rma((void *)&t67, (void *)NULL, cx, NULL, t65, t66);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
                    switch (t73) { case ((uint8_t)0ULL): goto ma71; case ((uint8_t)1ULL): goto ma69; case ((uint8_t)2ULL): goto ma70; default: goto ma72; }
ma69:;
                    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
                    if (t75 != ((uint8_t)1ULL)) goto mn74;
                    goto lc2;
                    goto me68;
mn74:;
ma70:;
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
                    if (t77 != ((uint8_t)2ULL)) goto mn76;
                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                    gm_l7 = t78;
                    gm_b16_8 t79 = gm_l7;
                    gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t79);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    goto me68;
mn76:;
ma71:;
                    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
                    if (t81 != ((uint8_t)0ULL)) goto mn80;
                    goto me68;
mn80:;
ma72:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 257, 13);
me68:;
                }
                goto ix60;
ie60:;
ix60:;
            }
            {
                gm_b16_8 t84 = gm_l4;
                gm_b16_8 t85;
                GM_ST(void *, (uint8_t *)&t85, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(1));
                uint8_t t86 = gm_f_starts_with_6vvt01e(t84, t85);
                if (!t86) goto ie83;
                {
                    gm_b16_8 t88 = gm_l4;
                    gm_tnpifv20irnph t89;
                    GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t88);
                    gm_tabb710d9mq2e t90;
                    gm_f_format_j0ggm04((void *)&t90, (void *)NULL, cx, NULL, t89);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_srcgs96ai7rj93j, 263, 45);
                    gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t90) + 8));
                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                    gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t92);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                }
                goto ix83;
ie83:;
ix83:;
            }
            {
                void * t93 = gm_l2;
                gm_b16_8 t94 = gm_l4;
                gm_tftb1odc739t5 t95 = gm_f_push_3ef746b(t93, t94);
                if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srcgs96ai7rj93j, 264, 9);
                gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
            }
lc2:;
        }
gm_ll6:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        uint8_t t97 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_render_36ljhn0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_render_36ljhn0 F;
    void * gm_l0; /* sm */
    void * gm_l1; /* diags */
    gm_t9qkh0rsooc8d gm_l2; /* format */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 387u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l1;
        gm_f_sort_s8jqb1h(t2);
    }
    {
        {
            void * t4 = gm_l1;
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 0) + 0));
            gm_l4 = t5;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l5;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l5;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t4gut2c9fj1kt t17 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t13 * 184));
                    F.l6 = t17;
                    {
                        void * t19 = gm_l0;
                        void * t20 = (void *)((uint8_t *)&F.l6);
                        gm_t9qkh0rsooc8d t21 = gm_l2;
                        void * t22 = (void *)((uint8_t *)&F.l3);
                        gm_b0_1 t23 = gm_f_render_pi7hvmf((void *)NULL, cx, NULL, t19, t20, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 456, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t25 = gm_l5;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l5 = t28;
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
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        gm_pf0nmuupvugn(cx, t30, gm_srcgs96ai7rj93j, 457, 5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t31 = gm_l1;
        void * t32 = (void *)(((uint8_t *)t31) + 0);
        gm_f_clear_ogqmkj3(t32);
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
__attribute__((aligned(4))) uint8_t gm_f_run_standalone_command_rnlbo5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_run_standalone_command_rnlbo5h F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* command */
    gm_b16_8 gm_l2; /* topic */
    gm_tpmdr2hmdhsqr gm_l3; /* help */
    gm_b16_8 gm_l4; /* message */
    GM_STACK_CHECK(&F, 1341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
        uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
        uint64_t t5 = (uintptr_t)((uint64_t)1ULL);
        gm_th2lqbo77pvsc t6;
        if (t5 < t4) {
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)t3 + t5 * 16));
        gm_th2lqbo77pvsc t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[16] = 1;
            t6 = t8;
        } else {
        gm_th2lqbo77pvsc t9;
        memset(&t9, 0, sizeof t9);
            t6 = t9;
        }
        gm_b16_8 t10;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t10 = t11;
        } else {
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(0));
            t10 = t12;
        }
        gm_l1 = t10;
    }
    {
        uint8_t t14 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s7l9fi1pqlcp5, 3) == 0));
        if (!t14) goto ie13;
        {
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t17 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t18 = (uintptr_t)((uint64_t)2ULL);
            if (t18 > (uintptr_t)t16 || (uintptr_t)t16 > (uintptr_t)t16) gm_trap_slice(t18, (uintptr_t)t16, t16, gm_srcgs96ai7rj93j, 169, 48);
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t17 + t18 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)((uintptr_t)t16 - t18));
            uint8_t t20 = gm_f_command_4iublbb((void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_node *pn21 = gm_find(cx, 621690230);
            if (!pn21) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 169, 27);
            if (pn21->kind == GM_NATIVE) {
                gm_unw.code = (uint8_t)t20;
                gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_lad;
            } else {
                ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn21->clauses[2])(pn21->env, pn21->hctx, pn21, t20);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b16_8 t23 = gm_l0;
        gm_th2lqbo77pvsc t24;
        gm_f_help_topic_l4hcqu9((void *)&t24, t23);
        if (!((((uint8_t *)&t24))[16] != 0)) goto ie22;
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_l2 = t25;
        {
            uint8_t t28 = 0;
            gm_b16_8 t29 = gm_l2;
            uint8_t t30 = gm_f_is_package_command_ufjt5rp(t29);
            if (!t30) {
                uint8_t t31 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_skijrg68314qg, 8) == 0));
                t28 = t31;
            } else t28 = 1;
            if (!t28) goto ie27;
            {
                gm_b16_8 t33 = gm_f_usage_8jc42fu((void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t34 = GM_LD(gm_b16_8, gm_crqb3tjd618);
                if ((GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t33)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8), gm_srcgs96ai7rj93j, 172, 13))) {
                    goto gm_lad;
                }
                if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srcgs96ai7rj93j, 172, 13)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t34) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t34) + 8), gm_srcgs96ai7rj93j, 172, 13))) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t35 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s4chbmodjkinv));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(6));
            gm_tpmdr2hmdhsqr t37;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
            gm_l3 = t37;
        }
        {
            uint8_t t39 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s8rsdirj4dmun, 3) == 0));
            if (!t39) goto ie38;
            {
                void * t41 = ((uint8_t *)&gm_l3);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcgs96ai7rj93j, 176, 50);
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t41 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                uint8_t t44 = gm_f_command_g3pch81((void *)NULL, cx, NULL, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_node *pn45 = gm_find(cx, 621690230);
                if (!pn45) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 176, 29);
                if (pn45->kind == GM_NATIVE) {
                    gm_unw.code = (uint8_t)t44;
                    gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_lad;
                } else {
                    ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn45->clauses[2])(pn45->env, pn45->hctx, pn45, t44);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix38;
ie38:;
ix38:;
        }
        {
            uint8_t t47 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s7l9fi1pqlcp5, 3) == 0));
            if (!t47) goto ie46;
            {
                void * t49 = ((uint8_t *)&gm_l3);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcgs96ai7rj93j, 177, 50);
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t49 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                uint8_t t52 = gm_f_command_4iublbb((void *)NULL, cx, NULL, t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_node *pn53 = gm_find(cx, 621690230);
                if (!pn53) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 177, 29);
                if (pn53->kind == GM_NATIVE) {
                    gm_unw.code = (uint8_t)t52;
                    gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_lad;
                } else {
                    ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn53->clauses[2])(pn53->env, pn53->hctx, pn53, t52);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix46;
ie46:;
ix46:;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        gm_b16_8 t55 = gm_l1;
        uint8_t t56 = gm_f_is_package_command_ufjt5rp(t55);
        if (!t56) goto ie54;
        {
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s19jg4ct3aq11));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(5));
            gm_b16_8 t59 = gm_l1;
            uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t61 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t62 = (uintptr_t)((uint64_t)2ULL);
            if (t62 > (uintptr_t)t60 || (uintptr_t)t60 > (uintptr_t)t60) gm_trap_slice(t62, (uintptr_t)t60, t60, gm_srcgs96ai7rj93j, 179, 85);
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t61 + t62 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)((uintptr_t)t60 - t62));
            uint8_t t64 = gm_f_run_command_s4fhqaq((void *)NULL, cx, NULL, t58, t59, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_node *pn65 = gm_find(cx, 621690230);
            if (!pn65) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 179, 42);
            if (pn65->kind == GM_NATIVE) {
                gm_unw.code = (uint8_t)t64;
                gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_lad;
            } else {
                ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn65->clauses[2])(pn65->env, pn65->hctx, pn65, t64);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        gm_b16_8 t67 = gm_l1;
        gm_tfeab279c16rr t68;
        gm_b16_8 t69;
        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sa7tr9aaspmun));
        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&t68 + 0, t69);
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s4chbmodjkinv));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&t68 + 16, t70);
        gm_b16_8 t71;
        GM_ST(void *, (uint8_t *)&t71, (void *)(gm_s3a1oio07ri1p));
        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t68 + 32, t71);
        uint8_t t72 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t68) + 0, ((uint8_t *)&t67))) t72 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t68) + 16, ((uint8_t *)&t67))) t72 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t68) + 32, ((uint8_t *)&t67))) t72 = 1;
        uint8_t t73 = (uint8_t)(t72);
        if (!t73) goto ie66;
        {
            gm_b16_8 t75 = gm_f_usage_8jc42fu((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_pfmaojsuds6a(cx, t75, gm_srcgs96ai7rj93j, 181, 9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t76 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        gm_b16_8 t78 = gm_l1;
        gm_t6j6smj7qoctu t79;
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(7));
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 0, t80);
        gm_b16_8 t81;
        GM_ST(void *, (uint8_t *)&t81, (void *)(gm_skou19srual26));
        GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(9));
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 16, t81);
        uint8_t t82 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t79) + 0, ((uint8_t *)&t78))) t82 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t79) + 16, ((uint8_t *)&t78))) t82 = 1;
        uint8_t t83 = (uint8_t)(t82);
        if (!t83) goto ie77;
        {
            gm_b16_8 t85 = GM_LD(gm_b16_8, gm_c4i68jc4bv0);
            if (gm_pf1m4b8q4e78(cx, gm_su5tga7voctrl, 6, gm_srcgs96ai7rj93j, 185, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t85) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t85)), GM_LD(uintptr_t, ((uint8_t *)&t85) + 8), gm_srcgs96ai7rj93j, 185, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srcgs96ai7rj93j, 185, 9)) {
                goto gm_lad;
            }
        }
        {
            uint8_t t86 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix77;
ie77:;
ix77:;
    }
    {
        uint8_t t88 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s8rsdirj4dmun, 3) == 0));
        if (!t88) goto ie87;
        {
            uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t91 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t92 = (uintptr_t)((uint64_t)2ULL);
            if (t92 > (uintptr_t)t90 || (uintptr_t)t90 > (uintptr_t)t90) gm_trap_slice(t92, (uintptr_t)t90, t90, gm_srcgs96ai7rj93j, 188, 48);
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t91 + t92 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)((uintptr_t)t90 - t92));
            uint8_t t94 = gm_f_command_g3pch81((void *)NULL, cx, NULL, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_node *pn95 = gm_find(cx, 621690230);
            if (!pn95) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 188, 27);
            if (pn95->kind == GM_NATIVE) {
                gm_unw.code = (uint8_t)t94;
                gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_lad;
            } else {
                ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn95->clauses[2])(pn95->env, pn95->hctx, pn95, t94);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix87;
ie87:;
ix87:;
    }
    {
        uint8_t t97 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sdjba9ailb8m1, 3) == 0));
        if (!t97) goto ie96;
        {
            uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t101 = (t100 > ((uint64_t)2ULL));
            if (!t101) goto ie99;
            uint64_t t102 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t102 >= (uintptr_t)(t103)) gm_trap_index(t102, t103, gm_srcgs96ai7rj93j, 190, 29);
            gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t102 * 16));
            gm_t6j6smj7qoctu t105;
            gm_b16_8 t106;
            GM_ST(void *, (uint8_t *)&t106, (void *)(gm_s4chbmodjkinv));
            GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&t105 + 0, t106);
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(gm_s3a1oio07ri1p));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(2));
            GM_ST(gm_b16_8, (uint8_t *)&t105 + 16, t107);
            uint8_t t108 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t105) + 0, ((uint8_t *)&t104))) t108 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t105) + 16, ((uint8_t *)&t104))) t108 = 1;
            uint8_t t109 = (uint8_t)(t108);
            if (!t109) goto ie99;
            {
                gm_b16_8 t111 = gm_f_usage_8jc42fu((void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_pfmaojsuds6a(cx, t111, gm_srcgs96ai7rj93j, 191, 13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t112 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix99;
ie99:;
ix99:;
        }
        {
            uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t115 = (t114 > ((uint64_t)2ULL));
            if (!t115) goto ie113;
            {
                uint64_t t117 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t117 >= (uintptr_t)(t118)) gm_trap_index(t117, t118, gm_srcgs96ai7rj93j, 194, 73);
                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t117 * 16));
                gm_tnpifv20irnph t120;
                GM_ST(gm_b16_8, ((uint8_t *)&t120 + 0), t119);
                gm_tabb710d9mq2e t121;
                gm_f_format_j0ggm04((void *)&t121, (void *)NULL, cx, NULL, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 8, gm_srcgs96ai7rj93j, 194, 39);
                gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&t121) + 8));
                gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t123);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix113;
ie113:;
ix113:;
        }
        {
            uint8_t t124 = gm_f_command_je557ls((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_node *pn125 = gm_find(cx, 621690230);
            if (!pn125) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 195, 9);
            if (pn125->kind == GM_NATIVE) {
                gm_unw.code = (uint8_t)t124;
                gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_lad;
            } else {
                ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn125->clauses[2])(pn125->env, pn125->hctx, pn125, t124);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix96;
ie96:;
ix96:;
    }
    {
        uint8_t t127 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s9qbs07c2ca5q, 4) == 0));
        if (!t127) goto ie126;
        {
            uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t130 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t131 = (uintptr_t)((uint64_t)2ULL);
            if (t131 > (uintptr_t)t129 || (uintptr_t)t129 > (uintptr_t)t129) gm_trap_slice(t131, (uintptr_t)t129, t129, gm_srcgs96ai7rj93j, 198, 34);
            gm_b16_8 t132;
            GM_ST(void *, (uint8_t *)&t132, (void *)(((uint8_t *)t130 + t131 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)((uintptr_t)t129 - t131));
            gm_tdpp5715iet0a t133;
            gm_f_parse_options_9st61k7((void *)&t133, (void *)NULL, cx, NULL, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&t133) + 0));
            switch (t139) { case ((uint8_t)0ULL): goto ma137; case ((uint8_t)1ULL): goto ma135; case ((uint8_t)2ULL): goto ma136; default: goto ma138; }
ma135:;
            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&t133) + 0));
            if (t141 != ((uint8_t)1ULL)) goto mn140;
            {
                gm_b16_8 t143 = gm_f_usage_8jc42fu((void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_pfmaojsuds6a(cx, t143, gm_srcgs96ai7rj93j, 200, 17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t144 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto me134;
mn140:;
ma136:;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&t133) + 0));
            if (t146 != ((uint8_t)2ULL)) goto mn145;
            gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&t133) + 8));
            gm_l4 = t147;
            gm_b16_8 t148 = gm_l4;
            gm_f_usage_error_898tao9((void *)NULL, cx, NULL, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me134;
mn145:;
ma137:;
            uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&t133) + 0));
            if (t150 != ((uint8_t)0ULL)) goto mn149;
            F.l5 = (*(gm_tvcg39brotose *)(((uint8_t *)&t133) + 8));
            {
                gm_tvcg39brotose t152 = F.l5;
                F.l6 = t152;
            }
            {
                gm_b16_8 t153 = GM_LD(gm_b16_8, gm_c4i68jc4bv0);
                GM_ST(gm_b16_8, (((uint8_t *)&F.l6) + 32), t153);
            }
            {
                uint8_t t154 = gm_f_command_run_06r7r92((void *)NULL, cx, NULL, &(*(gm_tvcg39brotose *)((uint8_t *)&F.l6)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_node *pn155 = gm_find(cx, 621690230);
                if (!pn155) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 207, 17);
                if (pn155->kind == GM_NATIVE) {
                    gm_unw.code = (uint8_t)t154;
                    gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_lad;
                } else {
                    ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn155->clauses[2])(pn155->env, pn155->hctx, pn155, t154);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me134;
mn149:;
ma138:;
            gm_trap("no match arm matched the scrutinee", gm_srcgs96ai7rj93j, 198, 9);
me134:;
        }
        goto ix126;
ie126:;
ix126:;
    }
    {
        uint8_t t156 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_usage_8jc42fu(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_usage_8jc42fu F;
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l0, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l0);
        gm_b16_8 t3 = GM_LD(gm_b16_8, gm_ctngbflld19);
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcgs96ai7rj93j, 93, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        void * t6 = (void *)((uint8_t *)&F.l0);
        gm_b0_1 t7 = gm_f_options_help_tlgad4v((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l0);
        gm_b16_8 t9 = GM_LD(gm_b16_8, gm_c50isunhcrj);
        gm_tftb1odc739t5 t10 = gm_f_extend_0cs85gs(t8, t9);
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcgs96ai7rj93j, 95, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 0));
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        uint8_t t14 = 0;
        F.ret = t13;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_usage_error_898tao9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_usage_error_898tao9 F;
    gm_b16_8 gm_l0; /* message */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcgs96ai7rj93j, 381, 5)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t2) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8), gm_srcgs96ai7rj93j, 381, 5))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_s8dbdqj1sqibj, 2, gm_srcgs96ai7rj93j, 381, 5)) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t3 = gm_f_usage_8jc42fu((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_pf0nmuupvugn(cx, t3, gm_srcgs96ai7rj93j, 382, 5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_node *pn4 = gm_find(cx, 621690230);
        if (!pn4) gm_trap("no handler installed for effect `Env`", gm_srcgs96ai7rj93j, 383, 5);
        if (pn4->kind == GM_NATIVE) {
            gm_unw.code = (uint8_t)((uint8_t)2ULL);
            gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_lad;
        } else {
            ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn4->clauses[2])(pn4->env, pn4->hctx, pn4, ((uint8_t)2ULL));
        }
        if (gm_unw.kind) {
            goto gm_lad;
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
void gm_pt_main_6cov5v1_25(void *req, gm_cont k) { struct gm_pr_main_6cov5v1_25 *r = (struct gm_pr_main_6cov5v1_25 *)req; gm_node *n = r->n; gm_b16_8 kb; memcpy(&kb, &k, sizeof kb); ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))n->clauses[3])(n->env, n->hctx, n, kb); }
void gm_wb_main_6cov5v1_0(gm_fiber *fb) {
    struct gm_fr_main_6cov5v1 *gm_Fp = (struct gm_fr_main_6cov5v1 *)fb->frame;
    void *env = fb->env; gm_node *cx = fb->cx; gm_node *inst = fb->inst;
    (void)env; (void)cx; (void)inst;
#define F (*gm_Fp)
    {
        gm_b0_1 t23 = gm_f_compiler_main_rh1a7t4((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_fprop;
        }
    }
    {
        gm_node *pn24 = gm_find(cx, 1597678050);
        if (!pn24) gm_trap("no handler installed for effect `Sched`", gm_srcgs96ai7rj93j, 138, 9);
        struct gm_pr_main_6cov5v1_25 pr26 = {pn24};
        gm_ctl_perform(pn24->fw, gm_pt_main_6cov5v1_25, &pr26, NULL);
        if (gm_unw.kind) {
            goto gm_fprop;
        }
    }
    F.w0 = GM_ZERO(gm_b0_1);
gm_fprop:
    gm_fiber_finish(fb);
#undef F
}
