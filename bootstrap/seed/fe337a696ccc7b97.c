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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; void * f2; } gm_t5r2drn9pjqkt;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t5r2drn9pjqkt) == 24 && _Alignof(gm_t5r2drn9pjqkt) == 8 && offsetof(gm_t5r2drn9pjqkt, f0) == 0 && offsetof(gm_t5r2drn9pjqkt, f1) == 8 && offsetof(gm_t5r2drn9pjqkt, f2) == 16, "layout of `PendingBody`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint64_t f0; gm_tiilmfd16v3fc f1; } gm_t4f5fr60fo50r;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
typedef struct { gm_tnpifv20irnph f0; uint8_t f1; uint8_t pe[7]; } gm_teseu4tn5o422;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
typedef struct { gm_tiilmfd16v3fc a[1]; } gm_tfe2ufaqq8re9;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4f5fr60fo50r) == 32 && _Alignof(gm_t4f5fr60fo50r) == 8 && offsetof(gm_t4f5fr60fo50r, f0) == 0 && offsetof(gm_t4f5fr60fo50r, f1) == 8, "layout of `(i: usize, x: EffectRef)`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teseu4tn5o422) == 24 && _Alignof(gm_teseu4tn5o422) == 8 && offsetof(gm_teseu4tn5o422, f0) == 0 && offsetof(gm_teseu4tn5o422, f1) == 16, "layout of `?EffectSet`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tfe2ufaqq8re9) == 24 && _Alignof(gm_tfe2ufaqq8re9) == 8 && offsetof(gm_tfe2ufaqq8re9, a) == 0, "layout of `[1]EffectRef`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_tqqhoivahirr0 f0; uint8_t f1; uint8_t pe[7]; } gm_tg93b272d11vo;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg93b272d11vo) == 144 && _Alignof(gm_tg93b272d11vo) == 8 && offsetof(gm_tg93b272d11vo, f0) == 0 && offsetof(gm_tg93b272d11vo, f1) == 136, "layout of `?FnSig`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
struct gm_fr_add_fn_t4t83qh {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* sig */
    gm_tdgfduqjheqdm l2; /* def */
    uint32_t ret;
};
struct gm_fr_bare_effect_ref_a5ofmji {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* args */
    gm_tiilmfd16v3fc ret;
};
struct gm_fr_bodiless_def_u9kqo09 {
    gm_frame h;
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_check_distinct_d4svq2h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_collect_terms_n8okg7m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_declare_effect_9n12hc8 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* params */
    gm_tf2orqcrphmek l9; /* p */
    gm_trqfvmjtt4sij l18; /* ops */
    gm_tc0sbs1tp6d7f l21; /* op */
    gm_trqfvmjtt4sij l22; /* op_params */
    gm_tf2orqcrphmek l25; /* p */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_declare_fn_6piolnr {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_t5v0c8ia6a5mo l7; /* p */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effect_param_named_h2jeoo7 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_effect_terms_bphj7th {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
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
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lp0ck0u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p6mn4us {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fresh_effect_2msfvhg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_splice_80pgqas {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_effect_sum_dd9le16 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_effects_kind_uutbf9u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_site_8sr7vam {
    gm_frame h;
    gm_tet9guhev5r4u l2; /* i */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_a7rbkus {
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
struct gm_fr_list_qs7fohu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_may_coincide_f6v7r98 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_named_effect_set_s5gn1lr {
    gm_frame h;
    gm_teseu4tn5o422 ret;
};
struct gm_fr_new_literal_fn_oqjp4lg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_new_nested_fn_r5u4ioo {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_provably_distinct_q4efcr7 {
    gm_frame h;
    gm_t53at80pfdus3 l3; /* ka */
    gm_t53at80pfdus3 l4; /* kb */
    uint8_t ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_effect_arg_4h6q4qv {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_resolve_effect_list_uhjlh6o {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* terms */
    gm_tnpifv20irnph ret;
};
struct gm_fr_resolve_effect_ref_bggirs2 {
    gm_frame h;
    gm_trqfvmjtt4sij l13; /* targs */
    gm_tc3oht4ksth9d l19; /* t */
    gm_tqu3m2l3n8mnc ret;
};
struct gm_fr_resolve_effect_set_o7rc5od {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* v */
    gm_tnpifv20irnph ret;
};
struct gm_fr_resolve_fn_sig_pamuhve {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* params */
    gm_trqfvmjtt4sij l7; /* generics */
    gm_tf2orqcrphmek l12; /* p */
    gm_t5v0c8ia6a5mo l37; /* p */
    gm_t5v0c8ia6a5mo l40; /* q */
    gm_tpv68m88f9lcr l47; /* c */
    gm_tg93b272d11vo ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
};
struct gm_fr_set_state_3j300fi {
    gm_frame h;
    gm_tagrkbqh054h7 l2; /* v */
    gm_b0_1 ret;
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
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_bare_effect_ref_a5ofmji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_bodiless_def_u9kqo09_c(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_distinct_d4svq2h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_terms_n8okg7m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_declare_effect_9n12hc8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_method_candidate_e20bo1i(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_effect_param_named_h2jeoo7(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_37dbjte(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4e7kcl2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_askmr98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kjp7rmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uvukia4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vilpuor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_effect_2msfvhg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_splice_80pgqas(gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_effect_sum_dd9le16(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_a7rbkus(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_may_coincide_f6v7r98(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_named_effect_set_s5gn1lr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_provably_distinct_q4efcr7(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_57kepgg(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_700doiq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o022ckl(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_puil3ql(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_set_o7rc5od(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_splice_tj8iedl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
extern const uint8_t gm_s0ad3307s8jmo[105];
extern const uint8_t gm_s0sivi9n1gpid[74];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s8p6h6db1ftub[32];
extern const uint8_t gm_sa7gjgqof9s56[44];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_sba4cjt08ps1i[58];
extern const uint8_t gm_sdsd71bjtv81c[98];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfvsgjcg86bbl[45];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_snnov1h58rig5[52];
extern const uint8_t gm_sp929adkqo1cd[93];
extern const uint8_t gm_spfda095eoqse[34];
extern const uint8_t gm_spskd7qirpoe3[25];
extern const uint8_t gm_sq5p8t05ccn5h[24];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcmhupam4se3h5[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sshv7blujc475[52];
extern const uint8_t gm_svurqcoqldl32[83];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bodiless_def_u9kqo09 F;
    gm_t9qkh0rsooc8d gm_l0; /* kind */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 1, 0, 7);
    memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 57, 0, 7);
    gm_t9qkh0rsooc8d t1 = gm_l0;
    gm_b16_8 t2 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t3 = gm_f_empty_slice_6trg8r7();
    gm_t9805qtonbsag t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 0, t1);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 8, t2);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 24, t3);
    GM_ST(void *, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 40, ((void *)0));
    GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 48, t4);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 56, ((uint8_t)0));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_effect_terms_bphj7th F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_qs7fohu((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t4 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t6 = 0;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6trg8r7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ito0pap F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lp0ck0u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p6mn4us F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_effect_2msfvhg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_effect_2msfvhg F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 216);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src0ih77f43mhf2, 279, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 216) + 0) + 8);
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 280, 13);
        uint64_t t12 = r10;
        uint32_t t13 = (uint32_t)t12;
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_effects_kind_uutbf9u F;
    void * gm_l0; /* t */
    gm_t08olq97rscn3 gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l1 = t5;
        {
            uint8_t t7 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l1) + 0) + 8) == 7) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l1) + 0)), gm_sgbumttiou4de, 7) == 0));
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t10 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_site_8sr7vam F;
    void * gm_l0; /* p */
    void * gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 127u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        void * t4 = t3;
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        void * t9 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t10 != ((uint8_t)49ULL)) goto ie8;
        gm_tet9guhev5r4u t11 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t9) + 8) + 8));
        F.l2 = t11;
        {
            uint8_t t13 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l2) + 0) + 0) + 8) == 15) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l2) + 0) + 0)), gm_sa9e77lq9q04p, 15) == 0));
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix8;
ie8:;
        {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix8:;
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
inline __attribute__((aligned(4))) void gm_f_list_a7rbkus(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_a7rbkus F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_p6mn4us();
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
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qs7fohu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lp0ck0u();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6) {
    struct gm_fr_new_literal_fn_oqjp4lg F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint64_t gm_l3; /* module */
    gm_b16_8 gm_l4; /* params */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tnpifv20irnph gm_l6; /* effects */
    GM_STACK_CHECK(&F, 174u);
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
        gm_t9qkh0rsooc8d t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
        gm_b16_8 t4 = gm_l1;
        gm_tl874t2atnlh3 t5 = gm_l2;
        uint64_t t6 = gm_l3;
        gm_b16_8 t7 = gm_l4;
        gm_t88rcucg1b7uk t8 = gm_l5;
        gm_tnpifv20irnph t9 = gm_l6;
        uint32_t t10 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t2, t3, t4, t5, t6, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
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
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 1488) + 0) + 8);
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1528) + 0) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1608) + 0) + 8);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 1568) + 0) + 8);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 0, t2);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 8, t4);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 16, t6);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 24, t8);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_set_state_3j300fi F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = gm_l1;
            gm_tagrkbqh054h7 t5;
            gm_f_state_a5bnsjb((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 304) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_srclv0dcbbfhves, 752, 5);
        gm_tagrkbqh054h7 t10 = F.l2;
        GM_ST(gm_tagrkbqh054h7, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t8) + 304) + 0)) + t7 * 216), t10);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 153u);
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
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t4, t5);
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 176) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 176) + 0)) + t3 * 4));
    F.ret = t6;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)6ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t8);
    gm_t53at80pfdus3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t9 + 8), t2);
    gm_t88rcucg1b7uk t10 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t10;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_add_fn_t4t83qh F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 344);
        gm_tftb1odc739t5 t4 = gm_f_push_472i7vl(t3, &(*(gm_tqqhoivahirr0 *)((uint8_t *)a1)));
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmhupam4se3h5, 466, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 384);
        gm_tftb1odc739t5 t9 = gm_f_push_roqi806(t8, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcmhupam4se3h5, 467, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 344) + 0) + 8);
        uint64_t r14; bool o15 = __builtin_sub_overflow(t13, ((uint64_t)1ULL), &r14);
        if (o15) gm_trap("integer overflow in `-`", gm_srcmhupam4se3h5, 468, 13);
        uint64_t t16 = r14;
        uint32_t t17 = (uint32_t)t16;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_bare_effect_ref_a5ofmji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_bare_effect_ref_a5ofmji F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* eff */
    gm_b16_8 gm_l2; /* params */
    uint8_t gm_l3; /* all_sets */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* p */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 595u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1048) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcmhupam4se3h5, 370, 15);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1048) + 0)) + t4 * 56) + 24));
        gm_l2 = t7;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t9 = (t8 > ((uint64_t)0ULL));
        gm_l3 = t9;
    }
    {
        {
            gm_b16_8 t11 = gm_l2;
            gm_l4 = t11;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l5;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l5;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 4));
                    uint32_t t22 = t21;
                    uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&t22));
                    gm_l6 = t25;
                    {
                        uint32_t t28 = gm_l6;
                        uint64_t t29 = (uint64_t)t28;
                        uint64_t t30 = (uintptr_t)t29;
                        void * t31 = gm_l0;
                        uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 1128) + 0) + 8);
                        if (t30 >= (uintptr_t)(t32)) gm_trap_index(t30, t32, gm_srcmhupam4se3h5, 373, 12);
                        uint8_t t33 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t31) + 1128) + 0)) + t30 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                        if (!t33) goto ie27;
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 372, 5);
me23:;
                }
lc12:;
                {
                    uint64_t t36 = gm_l5;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l5 = t39;
                }
            }
lb12:;
        }
    }
    {
        uint8_t t41 = gm_l3;
        uint8_t t42 = (uint8_t)!t41;
        if (!t42) goto ie40;
        {
            memset((uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 4, 0, 4);
            uint32_t t44 = gm_l1;
            gm_b16_8 t45 = gm_f_empty_slice_fh7svmj();
            gm_b16_8 t46 = t45;
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 0, t44);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 8, t47);
            goto gm_fdone;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        gm_f_list_vmvl81b((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t49 = gm_l2;
            gm_l8 = t49;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t51 = gm_l9;
                uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t53 = (t51 < t52);
                if (!t53) goto lb50;
                {
                    uint64_t t56 = gm_l9;
                    uint64_t t57 = (uintptr_t)t56;
                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint32_t t59 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t57 * 4));
                    uint32_t t60 = t59;
                    {
                        void * t64 = (void *)((uint8_t *)&F.l7);
                        void * t65 = gm_l0;
                        void * t66 = (void *)(((uint8_t *)t65) + 8);
                        gm_tnpifv20irnph t67 = gm_f_empty_effects_r0iv3pv();
                        gm_t53at80pfdus3 t68;
                        memset(&t68, 0, sizeof t68);
                        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)24ULL));
                        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t68 + 8), t67);
                        gm_t88rcucg1b7uk t69 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t66, &t68);
                        if (gm_unw.kind) {
                            goto gm_ll54;
                        }
                        gm_toa2vhe3l4hfr t70;
                        memset(&t70, 0, sizeof t70);
                        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t70 + 8), t69);
                        gm_tftb1odc739t5 t71 = gm_f_push_s5vr2a2(t64, &t70);
                        gm_tftb1odc739t5 t72 = t71;
                        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srcmhupam4se3h5, 377, 23);
                        gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                    }
                    goto me61;
mn62:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 377, 5);
me61:;
                }
lc50:;
                {
                    uint64_t t75 = gm_l9;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l9 = t78;
                }
            }
gm_ll54:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb50; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc50; }
            }
            goto gm_lad;
lb50:;
        }
    }
    {
        memset((uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 4, 0, 4);
        uint32_t t79 = gm_l1;
        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t81 = t80;
        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 0, t79);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 8, t82);
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
extern __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_bodiless_def_u9kqo09_c(gm_t9qkh0rsooc8d a0) { gm_tdgfduqjheqdm gm_r; gm_f_bodiless_def_u9kqo09(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_check_distinct_d4svq2h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_check_distinct_d4svq2h F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* refs */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_tiilmfd16v3fc gm_l6; /* a */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tiilmfd16v3fc gm_l9; /* b */
    GM_STACK_CHECK(&F, 707u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l2;
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
                    gm_t4f5fr60fo50r t10;
                    uint64_t t11 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    memcpy((uint8_t *)&t10 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 24), 24);
                    gm_t4f5fr60fo50r t15 = t10;
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    gm_l5 = t18;
                    gm_tiilmfd16v3fc t19 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t15) + 8));
                    gm_l6 = t19;
                    {
                        {
                            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            void * t23 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                            uint64_t t24 = gm_l5;
                            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                            if (o26) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 154, 23);
                            uint64_t t27 = r25;
                            uint64_t t28 = (uintptr_t)t27;
                            if (t28 > (uintptr_t)t22 || (uintptr_t)t22 > (uintptr_t)t22) gm_trap_slice(t28, (uintptr_t)t22, t22, gm_srcmhupam4se3h5, 154, 18);
                            gm_b16_8 t29;
                            GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t23 + t28 * 24)));
                            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)((uintptr_t)t22 - t28));
                            gm_l7 = t29;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t31 = gm_l8;
                                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t33 = (t31 < t32);
                                if (!t33) goto lb30;
                                {
                                    uint64_t t36 = gm_l8;
                                    uint64_t t37 = (uintptr_t)t36;
                                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_tiilmfd16v3fc t41 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t37 * 24));
                                    gm_l9 = t41;
                                    {
                                        void * t44 = gm_l0;
                                        void * t45 = (void *)((uint8_t *)&gm_l6);
                                        void * t46 = (void *)((uint8_t *)&gm_l9);
                                        uint8_t t47 = gm_f_may_coincide_f6v7r98(t44, t45, t46);
                                        if (!t47) goto ie43;
                                        {
                                            void * t49 = gm_l0;
                                            gm_tl874t2atnlh3 t50 = gm_l1;
                                            gm_t6j6smj7qoctu t51;
                                            void * t52 = gm_l0;
                                            void * t53 = (void *)(((uint8_t *)t52) + 8);
                                            void * t54 = (void *)((uint8_t *)&gm_l6);
                                            gm_b16_8 t55 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t53, t54);
                                            if (gm_unw.kind) {
                                                goto gm_ll34;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t55);
                                            void * t56 = gm_l0;
                                            void * t57 = (void *)(((uint8_t *)t56) + 8);
                                            void * t58 = (void *)((uint8_t *)&gm_l9);
                                            gm_b16_8 t59 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t57, t58);
                                            if (gm_unw.kind) {
                                                goto gm_ll34;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t59);
                                            gm_tabb710d9mq2e t60;
                                            gm_f_format_4e7kcl2((void *)&t60, (void *)NULL, cx, NULL, &t51);
                                            if (gm_unw.kind) {
                                                goto gm_ll34;
                                            }
                                            gm_tabb710d9mq2e t61 = t60;
                                            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_srcmhupam4se3h5, 156, 31);
                                            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
                                            gm_b16_8 t63 = t62;
                                            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                                            gm_b0_1 t65 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t49, t50, t64);
                                            if (gm_unw.kind) {
                                                goto gm_ll34;
                                            }
                                        }
                                        goto ix43;
ie43:;
ix43:;
                                    }
                                    goto me39;
mn40:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 154, 9);
me39:;
                                }
lc30:;
                                {
                                    uint64_t t67 = gm_l8;
                                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                                    uint64_t t70 = r68;
                                    gm_l8 = t70;
                                }
                            }
gm_ll34:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb30; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc30; }
                            }
                            goto gm_ll8;
lb30:;
                        }
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 153, 5);
me16:;
                }
lc4:;
                {
                    uint64_t t72 = gm_l4;
                    uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                    uint64_t t75 = r73;
                    gm_l4 = t75;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_terms_n8okg7m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collect_terms_n8okg7m F;
    void * gm_l0; /* e */
    void * gm_l1; /* out */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    GM_STACK_CHECK(&F, 111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)13ULL)) goto ie2;
        uint8_t t5 = GM_LD(uint8_t, ((((((uint8_t *)t3) + 8) + 8) + 0) + 0));
        if (t5 != ((uint8_t)0ULL)) goto ie2;
        uint8_t t6 = GM_LD(uint8_t, (((((((uint8_t *)t3) + 8) + 8) + 0) + 1) + 0));
        if (t6 != ((uint8_t)0ULL)) goto ie2;
        void * t7 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l2 = t7;
        void * t8 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 16));
        gm_l3 = t8;
        {
            void * t10 = gm_l2;
            void * t11 = gm_l1;
            gm_b0_1 t12 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t13 = gm_l3;
            void * t14 = gm_l1;
            gm_b0_1 t15 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
        {
            void * t17 = gm_l1;
            void * t18 = gm_l0;
            gm_tftb1odc739t5 t19 = gm_f_push_57kepgg(t17, t18);
            gm_tftb1odc739t5 t20 = t19;
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmhupam4se3h5, 221, 9);
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
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
__attribute__((aligned(4))) void gm_f_declare_effect_9n12hc8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4) {
    struct gm_fr_declare_effect_9n12hc8 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint64_t gm_l2; /* decl */
    gm_t08olq97rscn3 gm_l3; /* name */
    void * gm_l4; /* lit */
    uint32_t gm_l5; /* eff */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint8_t gm_l10; /* is_type */
    uint8_t gm_l11; /* is_effects */
    void * gm_l12; /* t */
    gm_t08olq97rscn3 gm_l13; /* i */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t08olq97rscn3 gm_l16; /* n */
    uint32_t gm_l17; /* pid */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_t88rcucg1b7uk gm_l26; /* ty */
    void * gm_l27; /* t */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t08olq97rscn3 gm_l30; /* n */
    gm_t88rcucg1b7uk gm_l31; /* ret */
    void * gm_l32; /* r */
    GM_STACK_CHECK(&F, 2678u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
        uint32_t t5 = gm_f_fresh_effect_2msfvhg((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t5;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t7 = gm_l4;
            gm_th2lqbo77pvsc t8 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t7) + 0));
            gm_th2lqbo77pvsc t9 = t8;
            gm_b16_8 t10;
            if (((((uint8_t *)&t9))[16] != 0)) {
                gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 0));
                t10 = t11;
            } else {
                gm_b16_8 t12 = gm_f_empty_slice_ito0pap();
                t10 = t12;
            }
            gm_l7 = t10;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l8;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l8;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_tf2orqcrphmek t24 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t20 * 56));
                    F.l9 = t24;
                    {
                        gm_l10 = ((uint8_t)0);
                    }
                    {
                        gm_l11 = ((uint8_t)0);
                    }
                    {
                        void * t27 = GM_LD(void *, (((uint8_t *)&F.l9) + 24));
                        void * t28 = t27;
                        if (!(GM_LD(void *, ((uint8_t *)&t28)) != 0)) goto ie26;
                        void * t29 = GM_LD(void *, ((uint8_t *)&t28));
                        gm_l12 = t29;
                        {
                            void * t32 = gm_l12;
                            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                            if (t33 != ((uint8_t)6ULL)) goto ie31;
                            gm_t08olq97rscn3 t34 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t32) + 8) + 8));
                            gm_l13 = t34;
                            {
                                uint8_t t36 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l13) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l13) + 0)), gm_s2737i1iq57nf, 4) == 0));
                                gm_l10 = t36;
                            }
                            goto ix31;
ie31:;
ix31:;
                        }
                        {
                            void * t37 = gm_l12;
                            uint8_t t38 = gm_f_is_effects_kind_uutbf9u(t37);
                            gm_l11 = t38;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    {
                        uint8_t t40 = gm_l10;
                        uint8_t t41 = (uint8_t)!t40;
                        if (!t41) goto ie39;
                        uint8_t t42 = gm_l11;
                        uint8_t t43 = (uint8_t)!t42;
                        if (!t43) goto ie39;
                        {
                            void * t45 = gm_l0;
                            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l9) + 40));
                            gm_b16_8 t47;
                            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sp929adkqo1cd));
                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(92));
                            gm_b0_1 t48 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t45, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                        }
                        goto ix39;
ie39:;
ix39:;
                    }
                    {
                        {
                            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                            gm_l14 = t50;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t52 = gm_l15;
                                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t54 = (t52 < t53);
                                if (!t54) goto lb51;
                                {
                                    uint64_t t57 = gm_l15;
                                    uint64_t t58 = (uintptr_t)t57;
                                    uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    gm_t08olq97rscn3 t62 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t58 * 32));
                                    gm_l16 = t62;
                                    {
                                        void * t64 = gm_l0;
                                        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 0));
                                        gm_tnnjeerolos0a t66;
                                        uint8_t t68 = gm_l11;
                                        if (!t68) goto ie67;
                                        gm_tnnjeerolos0a t70;
                                        GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)1ULL));
                                        gm_tnnjeerolos0a t71 = t70;
                                        t66 = t71;
                                        goto ix67;
ie67:;
                                        gm_tnnjeerolos0a t73;
                                        GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)0ULL));
                                        gm_tnnjeerolos0a t74 = t73;
                                        t66 = t74;
ix67:;
                                        uint32_t t75 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t64, t65, t66);
                                        if (gm_unw.kind) {
                                            goto gm_ll55;
                                        }
                                        gm_l17 = t75;
                                    }
                                    {
                                        void * t76 = (void *)((uint8_t *)&F.l6);
                                        uint32_t t77 = gm_l17;
                                        gm_tftb1odc739t5 t78 = gm_f_push_im9d4ro(t76, t77);
                                        gm_tftb1odc739t5 t79 = t78;
                                        if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_srcmhupam4se3h5, 419, 13);
                                        gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
                                    }
                                    {
                                        void * t81 = gm_l0;
                                        void * t82 = (void *)(((uint8_t *)t81) + 1488);
                                        gm_tep2drsl9408b t83;
                                        memset((uint8_t *)&t83 + 20, 0, 4);
                                        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t83 + 0, t84);
                                        void * t85 = gm_l0;
                                        void * t86 = (void *)(((uint8_t *)t85) + 8);
                                        uint32_t t87 = gm_l17;
                                        gm_t88rcucg1b7uk t88 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t86, t87);
                                        if (gm_unw.kind) {
                                            goto gm_ll55;
                                        }
                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t83 + 16, t88);
                                        gm_tftb1odc739t5 t89 = gm_f_push_hv483nm(t82, &t83);
                                        gm_tftb1odc739t5 t90 = t89;
                                        if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srcmhupam4se3h5, 420, 13);
                                        gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
                                    }
                                    goto me60;
mn61:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 417, 9);
me60:;
                                }
lc51:;
                                {
                                    uint64_t t93 = gm_l15;
                                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                                    uint64_t t96 = r94;
                                    gm_l15 = t96;
                                }
                            }
gm_ll55:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb51; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc51; }
                            }
                            goto gm_ll17;
lb51:;
                        }
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 407, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t98 = gm_l8;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l8 = t101;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_lad;
lb13:;
        }
    }
    {
        void * t102 = gm_l0;
        void * t103 = (void *)(((uint8_t *)t102) + 1048);
        gm_tammvh1iespi3 t104;
        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 0, t105);
        uint64_t t106 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t104 + 16, t106);
        gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 24, t107);
        gm_b16_8 t108 = gm_f_empty_slice_p6mn4us();
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 40, t108);
        gm_tftb1odc739t5 t109 = gm_f_push_puil3ql(t103, &t104);
        gm_tftb1odc739t5 t110 = t109;
        if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_srcmhupam4se3h5, 424, 5);
        gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
    }
    {
        void * t112 = gm_l0;
        uint64_t t113 = gm_l2;
        uint32_t t114 = gm_l5;
        gm_t6qooom8qa2do t115;
        memset(&t115, 0, sizeof t115);
        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)2ULL));
        GM_ST(uint32_t, ((uint8_t *)&t115 + 8), t114);
        gm_tagrkbqh054h7 t116;
        GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t116 + 8), t115);
        gm_b0_1 t117 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t112, t113, &t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_a7rbkus((void *)&F.l18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t119 = gm_l4;
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)t119) + 24));
            gm_l19 = t120;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t122 = gm_l20;
                uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t124 = (t122 < t123);
                if (!t124) goto lb121;
                {
                    uint64_t t127 = gm_l20;
                    uint64_t t128 = (uintptr_t)t127;
                    uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_tc0sbs1tp6d7f t132 = GM_LD(gm_tc0sbs1tp6d7f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t128 * 80));
                    F.l21 = t132;
                    {
                        gm_f_list_af74s0q((void *)&F.l22, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll125;
                        }
                    }
                    {
                        {
                            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 40));
                            gm_l23 = t135;
                        }
                        {
                            gm_l24 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t137 = gm_l24;
                                uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                uint8_t t139 = (t137 < t138);
                                if (!t139) goto lb136;
                                {
                                    uint64_t t142 = gm_l24;
                                    uint64_t t143 = (uintptr_t)t142;
                                    uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    gm_tf2orqcrphmek t147 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t143 * 56));
                                    F.l25 = t147;
                                    {
                                        void * t149 = gm_l0;
                                        void * t150 = (void *)(((uint8_t *)t149) + 8);
                                        gm_t88rcucg1b7uk t151 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t150);
                                        if (gm_unw.kind) {
                                            goto gm_ll140;
                                        }
                                        gm_l26 = t151;
                                    }
                                    {
                                        void * t153 = GM_LD(void *, (((uint8_t *)&F.l25) + 24));
                                        void * t154 = t153;
                                        if (!(GM_LD(void *, ((uint8_t *)&t154)) != 0)) goto ie152;
                                        void * t155 = GM_LD(void *, ((uint8_t *)&t154));
                                        gm_l27 = t155;
                                        {
                                            void * t157 = gm_l0;
                                            uint64_t t158 = gm_l1;
                                            void * t159 = gm_l27;
                                            gm_t88rcucg1b7uk t160 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t157, t158, t159);
                                            if (gm_unw.kind) {
                                                goto gm_ll140;
                                            }
                                            gm_l26 = t160;
                                        }
                                        goto ix152;
ie152:;
                                        {
                                            void * t162 = gm_l0;
                                            gm_tl874t2atnlh3 t163 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l25) + 40));
                                            gm_b16_8 t164;
                                            GM_ST(void *, (uint8_t *)&t164, (void *)(gm_s8p6h6db1ftub));
                                            GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(31));
                                            gm_b0_1 t165 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t162, t163, t164);
                                            if (gm_unw.kind) {
                                                goto gm_ll140;
                                            }
                                        }
ix152:;
                                    }
                                    {
                                        {
                                            gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                                            gm_l28 = t167;
                                        }
                                        {
                                            gm_l29 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t169 = gm_l29;
                                                uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                                uint8_t t171 = (t169 < t170);
                                                if (!t171) goto lb168;
                                                {
                                                    uint64_t t174 = gm_l29;
                                                    uint64_t t175 = (uintptr_t)t174;
                                                    uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                                    gm_t08olq97rscn3 t179 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t175 * 32));
                                                    gm_l30 = t179;
                                                    {
                                                        void * t181 = (void *)((uint8_t *)&F.l22);
                                                        gm_t5v0c8ia6a5mo t182;
                                                        memset((uint8_t *)&t182 + 49, 0, 7);
                                                        gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                                                        GM_ST(gm_b16_8, (uint8_t *)&t182 + 0, t183);
                                                        gm_t88rcucg1b7uk t184 = gm_l26;
                                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t182 + 16, t184);
                                                        gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l30) + 16));
                                                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t182 + 20, t185);
                                                        GM_ST(void *, (uint8_t *)&t182 + 32, ((void *)0));
                                                        gm_t9805qtonbsag t186;
                                                        memset(&t186, 0, sizeof t186);
                                                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t182 + 40, t186);
                                                        GM_ST(uint8_t, (uint8_t *)&t182 + 48, ((uint8_t)0));
                                                        gm_tftb1odc739t5 t187 = gm_f_push_o8u01sh(t181, &t182);
                                                        gm_tftb1odc739t5 t188 = t187;
                                                        if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 1, gm_srcmhupam4se3h5, 437, 17);
                                                        gm_b0_1 t189 = GM_LD(gm_b0_1, (((uint8_t *)&t188) + 1));
                                                    }
                                                    goto me177;
mn178:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 436, 13);
me177:;
                                                }
lc168:;
                                                {
                                                    uint64_t t191 = gm_l29;
                                                    uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                                                    uint64_t t194 = r192;
                                                    gm_l29 = t194;
                                                }
                                            }
lb168:;
                                        }
                                    }
                                    goto me145;
mn146:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 429, 9);
me145:;
                                }
lc136:;
                                {
                                    uint64_t t196 = gm_l24;
                                    uint64_t r197; bool o198 = __builtin_add_overflow(t196, ((uint64_t)1ULL), &r197);
                                    uint64_t t199 = r197;
                                    gm_l24 = t199;
                                }
                            }
gm_ll140:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb136; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc136; }
                            }
                            goto gm_ll125;
lb136:;
                        }
                    }
                    {
                        void * t200 = gm_l0;
                        void * t201 = (void *)(((uint8_t *)t200) + 8);
                        gm_t88rcucg1b7uk t202 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t201);
                        if (gm_unw.kind) {
                            goto gm_ll125;
                        }
                        gm_l31 = t202;
                    }
                    {
                        void * t204 = GM_LD(void *, (((uint8_t *)&F.l21) + 56));
                        void * t205 = t204;
                        if (!(GM_LD(void *, ((uint8_t *)&t205)) != 0)) goto ie203;
                        void * t206 = GM_LD(void *, ((uint8_t *)&t205));
                        gm_l32 = t206;
                        {
                            void * t208 = gm_l0;
                            uint64_t t209 = gm_l1;
                            void * t210 = gm_l32;
                            gm_t88rcucg1b7uk t211 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t208, t209, t210);
                            if (gm_unw.kind) {
                                goto gm_ll125;
                            }
                            gm_l31 = t211;
                        }
                        goto ix203;
ie203:;
ix203:;
                    }
                    {
                        uint64_t t213 = GM_LD(uintptr_t, (((uint8_t *)&F.l22) + 0) + 8);
                        uint8_t t214 = (t213 > ((uint64_t)0ULL));
                        if (!t214) goto ie212;
                        {
                            void * t216 = gm_l0;
                            uint64_t t217 = gm_l1;
                            uint64_t t218 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t219 = GM_LD(uintptr_t, (((uint8_t *)&F.l22) + 0) + 8);
                            if (t218 >= (uintptr_t)(t219)) gm_trap_index(t218, t219, gm_srcmhupam4se3h5, 442, 82);
                            gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l22) + 0)) + t218 * 56) + 16));
                            gm_b0_1 t221 = gm_f_declare_method_candidate_e20bo1i((void *)NULL, cx, NULL, t216, t217, &(*(gm_t08olq97rscn3 *)(((uint8_t *)&F.l21) + 0)), t220);
                            if (gm_unw.kind) {
                                goto gm_ll125;
                            }
                        }
                        goto ix212;
ie212:;
ix212:;
                    }
                    {
                        void * t222 = (void *)((uint8_t *)&F.l18);
                        gm_tv0k2vg3054a6 t223;
                        memset((uint8_t *)&t223 + 17, 0, 7);
                        memset((uint8_t *)&t223 + 44, 0, 4);
                        gm_b16_8 t224 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l21) + 0) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t223 + 0, t224);
                        gm_t9qkh0rsooc8d t225 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l21) + 32));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t223 + 16, t225);
                        gm_b16_8 t226 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t223 + 24, t226);
                        gm_t88rcucg1b7uk t227 = gm_l31;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t223 + 40, t227);
                        gm_tftb1odc739t5 t228 = gm_f_push_700doiq(t222, &t223);
                        gm_tftb1odc739t5 t229 = t228;
                        if (((((uint8_t *)&t229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t229) + 1, gm_srcmhupam4se3h5, 443, 9);
                        gm_b0_1 t230 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 1));
                    }
                    goto me130;
mn131:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 427, 5);
me130:;
                }
lc121:;
                {
                    uint64_t t232 = gm_l20;
                    uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                    uint64_t t235 = r233;
                    gm_l20 = t235;
                }
            }
gm_ll125:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb121; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc121; }
            }
            goto gm_lad;
lb121:;
        }
    }
    {
        void * t236 = gm_l0;
        void * t237 = (void *)(((uint8_t *)t236) + 1488);
        void * t238 = gm_l0;
        uint64_t t239 = GM_LD(uintptr_t, ((((uint8_t *)t238) + 1488) + 0) + 8);
        uint64_t t240 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint64_t r241; bool o242 = __builtin_sub_overflow(t239, t240, &r241);
        if (o242) gm_trap("integer overflow in `-`", gm_srcmhupam4se3h5, 445, 28);
        uint64_t t243 = r241;
        gm_f_truncate_7r782uc(t237, t243);
    }
    {
        uint32_t t244 = gm_l5;
        uint64_t t245 = (uint64_t)t244;
        uint64_t t246 = (uintptr_t)t245;
        void * t247 = gm_l0;
        uint64_t t248 = GM_LD(uintptr_t, ((((uint8_t *)t247) + 1048) + 0) + 8);
        if (t246 >= (uintptr_t)(t248)) gm_trap_index(t246, t248, gm_srcmhupam4se3h5, 446, 5);
        gm_b16_8 t249 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t247) + 1048) + 0)) + t246 * 56) + 40), t249);
    }
    {
        uint32_t t250 = gm_l5;
        gm_t6qooom8qa2do t251;
        memset(&t251, 0, sizeof t251);
        GM_ST(uint8_t, ((uint8_t *)&t251 + 0), ((uint8_t)2ULL));
        GM_ST(uint32_t, ((uint8_t *)&t251 + 8), t250);
        uint8_t t252 = 0;
        F.ret = t251;
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
__attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_declare_fn_6piolnr F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* lit */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l8; /* id */
    GM_STACK_CHECK(&F, 1701u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l3;
        void * t5 = (void *)(((uint8_t *)t4) + 0);
        void * t6 = gm_l3;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 72));
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_tg93b272d11vo t10;
        gm_f_resolve_fn_sig_pamuhve((void *)&t10, (void *)NULL, cx, NULL, t2, t3, t5, ((uint8_t)1), t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tg93b272d11vo t11 = t10;
        gm_tqqhoivahirr0 t12;
        if (((((uint8_t *)&t11))[136] != 0)) {
            gm_tqqhoivahirr0 t13 = GM_LD(gm_tqqhoivahirr0, (((uint8_t *)&t11) + 0));
            t12 = t13;
        } else {
            gm_t6qooom8qa2do t14;
            memset(&t14, 0, sizeof t14);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)11ULL));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        F.l4 = t12;
    }
    {
        {
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
            gm_l5 = t17;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t19 = gm_l6;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    uint64_t t24 = gm_l6;
                    uint64_t t25 = (uintptr_t)t24;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t5v0c8ia6a5mo t29 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t25 * 56));
                    F.l7 = t29;
                    {
                        uint8_t t32;
                        if ((((((uint8_t *)&F.l7) + 40))[4] != 0)) goto mn34;
                        t32 = ((uint8_t)1);
                        goto me33;
mn34:;
                        t32 = ((uint8_t)0);
                        goto me33;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 7, 12);
me33:;
                        if (!t32) goto ie31;
                        {
                            void * t37 = gm_l0;
                            uint64_t t38 = gm_l1;
                            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                            gm_b0_1 t40 = gm_f_declare_method_candidate_e20bo1i((void *)NULL, cx, NULL, t37, t38, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t39);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                        }
                        {
                            goto lb18;
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 6, 5);
me27:;
                }
lc18:;
                {
                    uint64_t t42 = gm_l6;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l6 = t45;
                }
            }
gm_ll22:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
            }
            goto gm_lad;
lb18:;
        }
    }
    {
        void * t46 = gm_l0;
        gm_tqqhoivahirr0 t47;
        memset((uint8_t *)&t47 + 28, 0, 4);
        memset((uint8_t *)&t47 + 60, 0, 4);
        memset((uint8_t *)&t47 + 129, 0, 7);
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t47 + 0, t48);
        gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t47 + 16, t49);
        uint64_t t50 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t47 + 32, t50);
        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&t47 + 40, t51);
        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t47 + 56, t52);
        gm_tnpifv20irnph t53 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l4) + 64));
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&t47 + 64, t53);
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 80));
        GM_ST(gm_b16_8, (uint8_t *)&t47 + 80, t54);
        gm_tqu3m2l3n8mnc t55;
        memset(&t55, 0, sizeof t55);
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t47 + 96, t55);
        GM_ST(uint8_t, (uint8_t *)&t47 + 128, ((uint8_t)0));
        gm_t9qkh0rsooc8d t56;
        GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
        gm_tdgfduqjheqdm t57;
        gm_f_bodiless_def_u9kqo09((void *)&t57, t56);
        uint32_t t58 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t46, &t47, &t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t58;
    }
    {
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 424);
        gm_t5r2drn9pjqkt t61;
        memset((uint8_t *)&t61 + 4, 0, 4);
        uint32_t t62 = gm_l8;
        GM_ST(uint32_t, (uint8_t *)&t61 + 0, t62);
        uint64_t t63 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t61 + 8, t63);
        void * t64 = gm_l3;
        GM_ST(void *, (uint8_t *)&t61 + 16, t64);
        gm_tftb1odc739t5 t65 = gm_f_push_o022ckl(t60, &t61);
        gm_tftb1odc739t5 t66 = t65;
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcmhupam4se3h5, 13, 5);
        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
    }
    {
        uint32_t t68 = gm_l8;
        gm_t6qooom8qa2do t69;
        memset(&t69, 0, sizeof t69);
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)5ULL));
        GM_ST(uint32_t, ((uint8_t *)&t69 + 8), t68);
        uint8_t t70 = 0;
        F.ret = t69;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_effect_param_named_h2jeoo7(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_param_named_h2jeoo7 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint32_t gm_l4; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 384u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            void * t7 = gm_l0;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_tkn42u7na3t81 t9 = gm_f_type_param_3av4r8i(t7, t8);
            gm_tkn42u7na3t81 t10 = t9;
            gm_t88rcucg1b7uk t11;
            if (((((uint8_t *)&t10))[4] != 0)) {
                gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t10) + 0));
                t11 = t12;
            } else {
                gm_tkn42u7na3t81 t13;
                memset(&t13, 0, sizeof t13);
                uint8_t t14 = 0;
                F.ret = t13;
                goto gm_fret;
            }
            gm_l3 = t11;
        }
        {
            void * t16 = gm_l0;
            void * t17 = (void *)(((uint8_t *)t16) + 8);
            gm_t88rcucg1b7uk t18 = gm_l3;
            gm_t53at80pfdus3 t19;
            gm_f_kind_jcnt4hm((void *)&t19, t17, GM_CONV(gm_csguankko4lmhd, t18));
            gm_t53at80pfdus3 t20 = t19;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
            if (t21 != ((uint8_t)19ULL)) goto ie15;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&t20) + 8));
            gm_l4 = t22;
            {
                uint32_t t25 = gm_l4;
                uint64_t t26 = (uint64_t)t25;
                uint64_t t27 = (uintptr_t)t26;
                void * t28 = gm_l0;
                uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)t28) + 1128) + 0) + 8);
                if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_srcmhupam4se3h5, 386, 8);
                uint8_t t30 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t28) + 1128) + 0)) + t27 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                if (!t30) goto ie24;
                {
                    gm_tkn42u7na3t81 t32;
                    memset(&t32, 0, sizeof t32);
                    uint8_t t33 = 0;
                    F.ret = t32;
                    goto gm_fret;
                }
                goto ix24;
ie24:;
ix24:;
            }
            {
                gm_t88rcucg1b7uk t34 = gm_l3;
                gm_tkn42u7na3t81 t35;
                GM_ST(uint8_t, (uint8_t *)&t35 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t35 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35), t34);
                ((uint8_t *)&t35)[4] = 1;
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
            goto ix15;
ie15:;
            {
                gm_tkn42u7na3t81 t38;
                memset(&t38, 0, sizeof t38);
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
ix15:;
        }
        goto ix2;
ie2:;
        {
            gm_tkn42u7na3t81 t41;
            memset(&t41, 0, sizeof t41);
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_splice_80pgqas(gm_tnpifv20irnph a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_splice_80pgqas F;
    gm_tnpifv20irnph gm_l0; /* set */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_tiilmfd16v3fc gm_l3; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l0) + 0));
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
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 24));
                    gm_l3 = t15;
                    {
                        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t19 = ((t18 == ((uint32_t)0ULL)));
                        if (!t19) goto ie17;
                        {
                            uint8_t t21 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 227, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t23 = gm_l2;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l2 = t26;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t27 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_effect_sum_dd9le16(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_is_effect_sum_dd9le16 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* first */
    void * gm_l4; /* head */
    void * gm_l5; /* callee */
    gm_tf5srh4mp4b3i gm_l6; /* d */
    gm_t08olq97rscn3 gm_l7; /* id */
    void * gm_l8; /* base */
    gm_t08olq97rscn3 gm_l9; /* name */
    uint64_t gm_l10; /* m */
    uint64_t gm_l11; /* decl */
    GM_STACK_CHECK(&F, 989u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        void * t3 = gm_l2;
        gm_b16_8 t4 = gm_f_effect_terms_bphj7th((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8);
        if (t2 >= (uintptr_t)(t6)) gm_trap_index(t2, t6, gm_srcmhupam4se3h5, 261, 14);
        void * t7 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t5)) + t2 * 8));
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l3;
        gm_l4 = t8;
    }
    {
        void * t10 = gm_l3;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
        if (t11 != ((uint8_t)19ULL)) goto ie9;
        void * t12 = GM_LD(void *, (((((uint8_t *)t10) + 8) + 8) + 0));
        gm_l5 = t12;
        {
            void * t14 = gm_l5;
            gm_l4 = t14;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_tf5srh4mp4b3i t15;
        memset(&t15, 0, sizeof t15);
        gm_l6 = t15;
    }
    {
        void * t16 = gm_l4;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma20; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma20; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma20; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma20; case ((uint8_t)10ULL): goto ma20; case ((uint8_t)11ULL): goto ma20; case ((uint8_t)12ULL): goto ma20; case ((uint8_t)13ULL): goto ma20; case ((uint8_t)14ULL): goto ma20; case ((uint8_t)15ULL): goto ma20; case ((uint8_t)16ULL): goto ma20; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma20; case ((uint8_t)19ULL): goto ma20; case ((uint8_t)20ULL): goto ma20; case ((uint8_t)21ULL): goto ma20; case ((uint8_t)22ULL): goto ma19; case ((uint8_t)23ULL): goto ma20; case ((uint8_t)24ULL): goto ma20; case ((uint8_t)25ULL): goto ma20; case ((uint8_t)26ULL): goto ma20; case ((uint8_t)27ULL): goto ma20; case ((uint8_t)28ULL): goto ma20; case ((uint8_t)29ULL): goto ma20; case ((uint8_t)30ULL): goto ma20; case ((uint8_t)31ULL): goto ma20; case ((uint8_t)32ULL): goto ma20; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma20; case ((uint8_t)35ULL): goto ma20; case ((uint8_t)36ULL): goto ma20; case ((uint8_t)37ULL): goto ma20; case ((uint8_t)38ULL): goto ma20; case ((uint8_t)39ULL): goto ma20; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma20; case ((uint8_t)42ULL): goto ma20; case ((uint8_t)43ULL): goto ma20; case ((uint8_t)44ULL): goto ma20; case ((uint8_t)45ULL): goto ma20; case ((uint8_t)46ULL): goto ma20; case ((uint8_t)47ULL): goto ma20; case ((uint8_t)48ULL): goto ma20; case ((uint8_t)49ULL): goto ma20; case ((uint8_t)50ULL): goto ma20; default: goto ma20; }
ma18:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
        if (t24 != ((uint8_t)6ULL)) goto mn23;
        gm_t08olq97rscn3 t25 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t16) + 8) + 8));
        gm_l7 = t25;
        {
            void * t27 = gm_l0;
            uint64_t t28 = gm_l1;
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tf5srh4mp4b3i t30 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t30;
        }
        goto me17;
mn23:;
ma19:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
        if (t32 != ((uint8_t)22ULL)) goto mn31;
        void * t33 = GM_LD(void *, (((((uint8_t *)t16) + 8) + 8) + 0));
        gm_l8 = t33;
        gm_t08olq97rscn3 t34 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t16) + 8) + 8) + 8));
        gm_l9 = t34;
        {
            void * t37 = gm_l0;
            uint64_t t38 = gm_l1;
            void * t39 = gm_l8;
            gm_tf5srh4mp4b3i t40 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t37, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t41 = t40;
            if (!((((uint8_t *)&t41))[8] != 0)) goto ie36;
            uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&t41));
            gm_l10 = t42;
            {
                void * t44 = gm_l0;
                uint64_t t45 = gm_l10;
                uint64_t t46 = gm_l1;
                gm_tf5srh4mp4b3i t47 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t44, t45, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l9)), t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t47;
            }
            goto ix36;
ie36:;
ix36:;
        }
        goto me17;
mn31:;
ma20:;
        goto me17;
mn48:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 265, 5);
me17:;
    }
    {
        gm_tf5srh4mp4b3i t50 = gm_l6;
        gm_tf5srh4mp4b3i t51 = t50;
        uint64_t t52;
        if (((((uint8_t *)&t51))[8] != 0)) {
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&t51) + 0));
            t52 = t53;
        } else {
            uint8_t t54 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l11 = t52;
    }
    {
        void * t55 = gm_l0;
        uint64_t t56 = gm_l11;
        gm_t6qooom8qa2do t57;
        gm_f_resolve_decl_pus042v((void *)&t57, (void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t58 = t57;
        uint8_t t59;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        switch (t66) { case ((uint8_t)0ULL): goto ma64; case ((uint8_t)1ULL): goto ma64; case ((uint8_t)2ULL): goto ma61; case ((uint8_t)3ULL): goto ma62; case ((uint8_t)4ULL): goto ma63; case ((uint8_t)5ULL): goto ma64; case ((uint8_t)6ULL): goto ma64; case ((uint8_t)7ULL): goto ma64; case ((uint8_t)8ULL): goto ma64; case ((uint8_t)9ULL): goto ma64; case ((uint8_t)10ULL): goto ma64; case ((uint8_t)11ULL): goto ma64; default: goto ma64; }
ma61:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        if (t68 != ((uint8_t)2ULL)) goto mn67;
        t59 = ((uint8_t)1);
        goto me60;
mn67:;
ma62:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        if (t70 != ((uint8_t)3ULL)) goto mn69;
        t59 = ((uint8_t)1);
        goto me60;
mn69:;
ma63:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        if (t72 != ((uint8_t)4ULL)) goto mn71;
        t59 = ((uint8_t)1);
        goto me60;
mn71:;
ma64:;
        t59 = ((uint8_t)0);
        goto me60;
mn73:;
ma65:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 273, 12);
me60:;
        uint8_t t74 = 0;
        F.ret = t59;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_may_coincide_f6v7r98(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_may_coincide_f6v7r98 F;
    void * gm_l0; /* s */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t88rcucg1b7uk gm_l6; /* x */
    gm_t88rcucg1b7uk gm_l7; /* y */
    GM_STACK_CHECK(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l1;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 0));
        void * t6 = gm_l2;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t6) + 0));
        uint8_t t8 = (!(t5 == t7));
        if (!t8) {
            void * t9 = gm_l1;
            void * t10 = gm_l2;
            int32_t t11 = gm_f_cmp_ref_8ntrqll(t9, t10);
            uint8_t t12 = ((t11 == ((int32_t)0LL)));
            t3 = t12;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t16 = gm_l1;
            uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 8) + 8);
            void * t18 = gm_l2;
            uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 8) + 8);
            uint64_t t20 = (t17 < t19 ? t17 : t19);
            gm_l3 = t20;
        }
        {
            for (;;) {
                uint64_t t22 = gm_l4;
                uint64_t t23 = gm_l3;
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t t28 = t27;
                    uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&t28));
                    gm_l5 = t31;
                    {
                        uint64_t t34 = gm_l5;
                        uint64_t t35 = (uintptr_t)t34;
                        void * t36 = gm_l1;
                        uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 8) + 8);
                        if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srcmhupam4se3h5, 165, 24);
                        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 8)) + t35 * 40) + 0));
                        if (t38 != ((uint8_t)0ULL)) goto ie33;
                        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 8)) + t35 * 40) + 8));
                        gm_l6 = t39;
                        uint64_t t40 = gm_l5;
                        uint64_t t41 = (uintptr_t)t40;
                        void * t42 = gm_l2;
                        uint64_t t43 = GM_LD(uintptr_t, (((uint8_t *)t42) + 8) + 8);
                        if (t41 >= (uintptr_t)(t43)) gm_trap_index(t41, t43, gm_srcmhupam4se3h5, 165, 50);
                        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t42) + 8)) + t41 * 40) + 0));
                        if (t44 != ((uint8_t)0ULL)) goto ie33;
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t42) + 8)) + t41 * 40) + 8));
                        gm_l7 = t45;
                        {
                            void * t48 = gm_l0;
                            gm_t88rcucg1b7uk t49 = gm_l6;
                            gm_t88rcucg1b7uk t50 = gm_l7;
                            uint8_t t51 = gm_f_provably_distinct_q4efcr7(t48, GM_CONV(gm_csguankko4lmhd, t49), GM_CONV(gm_csguankko4lmhd, t50));
                            if (!t51) goto ie47;
                            {
                                uint8_t t53 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix47;
ie47:;
ix47:;
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    goto me29;
mn30:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 164, 5);
me29:;
                }
lc21:;
                {
                    uint64_t t55 = gm_l4;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l4 = t58;
                }
            }
lb21:;
        }
    }
    {
        uint8_t t59 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_named_effect_set_s5gn1lr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_named_effect_set_s5gn1lr F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint64_t gm_l3; /* d */
    gm_t08olq97rscn3 gm_l4; /* id */
    void * gm_l5; /* base */
    gm_t08olq97rscn3 gm_l6; /* name */
    uint64_t gm_l7; /* m */
    gm_tnpifv20irnph gm_l8; /* set */
    GM_STACK_CHECK(&F, 1050u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)6ULL)) goto mn9;
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t11;
        {
            void * t13 = gm_l0;
            uint64_t t14 = gm_l1;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tf5srh4mp4b3i t16 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t17 = t16;
            uint64_t t18;
            if (((((uint8_t *)&t17))[8] != 0)) {
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                t18 = t19;
            } else {
                gm_teseu4tn5o422 t20;
                memset(&t20, 0, sizeof t20);
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            gm_l3 = t18;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)22ULL)) goto mn22;
        void * t24 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l5 = t24;
        gm_t08olq97rscn3 t25 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l6 = t25;
        {
            void * t27 = gm_l0;
            uint64_t t28 = gm_l1;
            void * t29 = gm_l5;
            gm_tf5srh4mp4b3i t30 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t31 = t30;
            uint64_t t32;
            if (((((uint8_t *)&t31))[8] != 0)) {
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&t31) + 0));
                t32 = t33;
            } else {
                gm_teseu4tn5o422 t34;
                memset(&t34, 0, sizeof t34);
                uint8_t t35 = 0;
                F.ret = t34;
                goto gm_fret;
            }
            gm_l7 = t32;
        }
        {
            void * t36 = gm_l0;
            uint64_t t37 = gm_l7;
            uint64_t t38 = gm_l1;
            gm_tf5srh4mp4b3i t39 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t36, t37, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l6)), t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t40 = t39;
            uint64_t t41;
            if (((((uint8_t *)&t40))[8] != 0)) {
                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&t40) + 0));
                t41 = t42;
            } else {
                gm_teseu4tn5o422 t43;
                memset(&t43, 0, sizeof t43);
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
            }
            gm_l3 = t41;
        }
        goto me3;
mn22:;
ma6:;
        gm_teseu4tn5o422 t46;
        memset(&t46, 0, sizeof t46);
        uint8_t t47 = 0;
        F.ret = t46;
        goto gm_fret;
        goto me3;
mn45:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 279, 5);
me3:;
    }
    {
        void * t49 = gm_l0;
        uint64_t t50 = gm_l3;
        gm_t6qooom8qa2do t51;
        gm_f_resolve_decl_pus042v((void *)&t51, (void *)NULL, cx, NULL, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t52 = t51;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
        if (t53 != ((uint8_t)4ULL)) goto ie48;
        gm_tnpifv20irnph t54 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t52) + 8));
        gm_l8 = t54;
        {
            gm_tnpifv20irnph t56 = gm_l8;
            gm_teseu4tn5o422 t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 20, 0);
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t57), t56);
            ((uint8_t *)&t57)[16] = 1;
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix48;
ie48:;
        {
            gm_teseu4tn5o422 t60;
            memset(&t60, 0, sizeof t60);
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
ix48:;
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
extern __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6);
inline __attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7) {
    struct gm_fr_new_nested_fn_r5u4ioo F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l4; /* module */
    gm_b16_8 gm_l5; /* params */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    gm_tnpifv20irnph gm_l7; /* effects */
    GM_STACK_CHECK(&F, 423u);
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
        void * t2 = gm_l0;
        gm_tqqhoivahirr0 t3;
        memset((uint8_t *)&t3 + 28, 0, 4);
        memset((uint8_t *)&t3 + 60, 0, 4);
        memset((uint8_t *)&t3 + 129, 0, 7);
        gm_b16_8 t4 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 0, t4);
        gm_tl874t2atnlh3 t5 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t3 + 16, t5);
        uint64_t t6 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t3 + 32, t6);
        gm_b16_8 t7 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 40, t7);
        gm_t88rcucg1b7uk t8 = gm_l6;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t3 + 56, t8);
        gm_tnpifv20irnph t9 = gm_l7;
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&t3 + 64, t9);
        gm_b16_8 t10 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 80, t10);
        gm_tqu3m2l3n8mnc t11;
        memset(&t11, 0, sizeof t11);
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t3 + 96, t11);
        GM_ST(uint8_t, (uint8_t *)&t3 + 128, ((uint8_t)0));
        gm_t9qkh0rsooc8d t12 = gm_l1;
        gm_tdgfduqjheqdm t13;
        gm_f_bodiless_def_u9kqo09((void *)&t13, t12);
        uint32_t t14 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t2, &t3, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t15 = 0;
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
__attribute__((aligned(4))) uint8_t gm_f_provably_distinct_q4efcr7(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_provably_distinct_q4efcr7 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* a */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* y */
    gm_t88rcucg1b7uk gm_l7; /* x */
    gm_t88rcucg1b7uk gm_l8; /* y */
    gm_t88rcucg1b7uk gm_l9; /* x */
    gm_t88rcucg1b7uk gm_l10; /* y */
    gm_t88rcucg1b7uk gm_l11; /* x */
    gm_t88rcucg1b7uk gm_l12; /* y */
    uint32_t gm_l13; /* x */
    gm_b16_8 gm_l14; /* xs */
    uint32_t gm_l15; /* y */
    gm_b16_8 gm_l16; /* ys */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_t88rcucg1b7uk gm_l20; /* p */
    gm_t88rcucg1b7uk gm_l21; /* q */
    gm_b16_8 gm_l22; /* x */
    gm_b16_8 gm_l23; /* y */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    GM_STACK_CHECK(&F, 1061u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        uint8_t t6 = gm_f_has_param_u9t0efs(t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) goto ie2;
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l2;
        uint8_t t11 = gm_f_has_param_u9t0efs(t9, GM_CONV(gm_csguankko4lmhd, t10));
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie2;
        {
            gm_t88rcucg1b7uk t14 = gm_l1;
            gm_t88rcucg1b7uk t15 = gm_l2;
            gm_t88rcucg1b7uk t16 = t14;
            gm_t88rcucg1b7uk t17 = t15;
            uint8_t t18 = (!gm_eq9m6mvtmgde(((uint8_t *)&t16), ((uint8_t *)&t17)));
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)t20) + 8);
        gm_t88rcucg1b7uk t22 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l3, t21, GM_CONV(gm_csguankko4lmhd, t22));
    }
    {
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 8);
        gm_t88rcucg1b7uk t25 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l4, t24, GM_CONV(gm_csguankko4lmhd, t25));
    }
    {
        uint8_t t27 = 0;
        uint8_t t28;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t31 != ((uint8_t)19ULL)) goto mn30;
        t28 = ((uint8_t)1);
        goto me29;
mn30:;
        t28 = ((uint8_t)0);
        goto me29;
mn32:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 176, 8);
me29:;
        if (!t28) {
            uint8_t t33;
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t36 != ((uint8_t)19ULL)) goto mn35;
            t33 = ((uint8_t)1);
            goto me34;
mn35:;
            t33 = ((uint8_t)0);
            goto me34;
mn37:;
            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 176, 27);
me34:;
            t27 = t33;
        } else t27 = 1;
        if (!t27) goto ie26;
        {
            uint8_t t39 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        switch (t49) { case ((uint8_t)0ULL): goto ma47; case ((uint8_t)1ULL): goto ma47; case ((uint8_t)2ULL): goto ma47; case ((uint8_t)3ULL): goto ma47; case ((uint8_t)4ULL): goto ma47; case ((uint8_t)5ULL): goto ma47; case ((uint8_t)6ULL): goto ma47; case ((uint8_t)7ULL): goto ma47; case ((uint8_t)8ULL): goto ma41; case ((uint8_t)9ULL): goto ma43; case ((uint8_t)10ULL): goto ma42; case ((uint8_t)11ULL): goto ma47; case ((uint8_t)12ULL): goto ma46; case ((uint8_t)13ULL): goto ma44; case ((uint8_t)14ULL): goto ma47; case ((uint8_t)15ULL): goto ma47; case ((uint8_t)16ULL): goto ma47; case ((uint8_t)17ULL): goto ma45; case ((uint8_t)18ULL): goto ma47; case ((uint8_t)19ULL): goto ma47; case ((uint8_t)20ULL): goto ma47; case ((uint8_t)21ULL): goto ma47; case ((uint8_t)22ULL): goto ma47; case ((uint8_t)23ULL): goto ma47; case ((uint8_t)24ULL): goto ma47; case ((uint8_t)25ULL): goto ma47; case ((uint8_t)26ULL): goto ma47; default: goto ma47; }
ma41:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t51 != ((uint8_t)8ULL)) goto mn50;
        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l5 = t52;
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t54 != ((uint8_t)8ULL)) goto ie53;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l6 = t55;
        {
            void * t57 = gm_l0;
            gm_t88rcucg1b7uk t58 = gm_l5;
            gm_t88rcucg1b7uk t59 = gm_l6;
            uint8_t t60 = gm_f_provably_distinct_q4efcr7(t57, GM_CONV(gm_csguankko4lmhd, t58), GM_CONV(gm_csguankko4lmhd, t59));
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
        goto ix53;
ie53:;
ix53:;
        goto me40;
mn50:;
ma42:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t63 != ((uint8_t)10ULL)) goto mn62;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l7 = t64;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t66 != ((uint8_t)10ULL)) goto ie65;
        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l8 = t67;
        {
            void * t69 = gm_l0;
            gm_t88rcucg1b7uk t70 = gm_l7;
            gm_t88rcucg1b7uk t71 = gm_l8;
            uint8_t t72 = gm_f_provably_distinct_q4efcr7(t69, GM_CONV(gm_csguankko4lmhd, t70), GM_CONV(gm_csguankko4lmhd, t71));
            uint8_t t73 = 0;
            F.ret = t72;
            goto gm_fret;
        }
        goto ix65;
ie65:;
ix65:;
        goto me40;
mn62:;
ma43:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t75 != ((uint8_t)9ULL)) goto mn74;
        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l9 = t76;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t78 != ((uint8_t)9ULL)) goto ie77;
        gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l10 = t79;
        {
            void * t81 = gm_l0;
            gm_t88rcucg1b7uk t82 = gm_l9;
            gm_t88rcucg1b7uk t83 = gm_l10;
            uint8_t t84 = gm_f_provably_distinct_q4efcr7(t81, GM_CONV(gm_csguankko4lmhd, t82), GM_CONV(gm_csguankko4lmhd, t83));
            uint8_t t85 = 0;
            F.ret = t84;
            goto gm_fret;
        }
        goto ix77;
ie77:;
ix77:;
        goto me40;
mn74:;
ma44:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t87 != ((uint8_t)13ULL)) goto mn86;
        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
        gm_l11 = t88;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t90 != ((uint8_t)13ULL)) goto ie89;
        gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l12 = t91;
        {
            void * t93 = gm_l0;
            gm_t88rcucg1b7uk t94 = gm_l11;
            gm_t88rcucg1b7uk t95 = gm_l12;
            uint8_t t96 = gm_f_provably_distinct_q4efcr7(t93, GM_CONV(gm_csguankko4lmhd, t94), GM_CONV(gm_csguankko4lmhd, t95));
            uint8_t t97 = 0;
            F.ret = t96;
            goto gm_fret;
        }
        goto ix89;
ie89:;
ix89:;
        goto me40;
mn86:;
ma45:;
        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t99 != ((uint8_t)17ULL)) goto mn98;
        uint32_t t100 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l13 = t100;
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 8));
        gm_l14 = t101;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t103 != ((uint8_t)17ULL)) goto ie102;
        uint32_t t104 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l15 = t104;
        gm_b16_8 t105 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l16 = t105;
        {
            void * t108 = gm_l0;
            void * t109 = (void *)(((uint8_t *)t108) + 8);
            uint32_t t110 = gm_l13;
            uint32_t t111 = gm_f_template_9o92cvv(t109, t110);
            void * t112 = gm_l0;
            void * t113 = (void *)(((uint8_t *)t112) + 8);
            uint32_t t114 = gm_l15;
            uint32_t t115 = gm_f_template_9o92cvv(t113, t114);
            uint8_t t116 = (!(t111 == t115));
            if (!t116) goto ie107;
            {
                uint8_t t118 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix107;
ie107:;
ix107:;
        }
        {
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint64_t t122 = (t120 < t121 ? t120 : t121);
                gm_l17 = t122;
            }
            {
                for (;;) {
                    uint64_t t124 = gm_l18;
                    uint64_t t125 = gm_l17;
                    uint8_t t126 = (t124 < t125);
                    if (!t126) goto lb123;
                    {
                        uint64_t t129 = gm_l18;
                        uint64_t t130 = t129;
                        uint64_t t133 = GM_LD(uint64_t, ((uint8_t *)&t130));
                        gm_l19 = t133;
                        {
                            uint64_t t136 = gm_l19;
                            uint64_t t137 = (uintptr_t)t136;
                            uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            if (t137 >= (uintptr_t)(t138)) gm_trap_index(t137, t138, gm_srcmhupam4se3h5, 185, 32);
                            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t137 * 40) + 0));
                            if (t139 != ((uint8_t)0ULL)) goto ie135;
                            gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t137 * 40) + 8));
                            gm_l20 = t140;
                            uint64_t t141 = gm_l19;
                            uint64_t t142 = (uintptr_t)t141;
                            uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            if (t142 >= (uintptr_t)(t143)) gm_trap_index(t142, t143, gm_srcmhupam4se3h5, 185, 54);
                            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t142 * 40) + 0));
                            if (t144 != ((uint8_t)0ULL)) goto ie135;
                            gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t142 * 40) + 8));
                            gm_l21 = t145;
                            {
                                void * t148 = gm_l0;
                                gm_t88rcucg1b7uk t149 = gm_l20;
                                gm_t88rcucg1b7uk t150 = gm_l21;
                                uint8_t t151 = gm_f_provably_distinct_q4efcr7(t148, GM_CONV(gm_csguankko4lmhd, t149), GM_CONV(gm_csguankko4lmhd, t150));
                                if (!t151) goto ie147;
                                {
                                    uint8_t t153 = 0;
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto ix147;
ie147:;
ix147:;
                            }
                            goto ix135;
ie135:;
ix135:;
                        }
                        goto me131;
mn132:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 184, 13);
me131:;
                    }
lc123:;
                    {
                        uint64_t t155 = gm_l18;
                        uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                        uint64_t t158 = r156;
                        gm_l18 = t158;
                    }
                }
lb123:;
            }
        }
        {
            uint8_t t159 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix102;
ie102:;
ix102:;
        goto me40;
mn98:;
ma46:;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t161 != ((uint8_t)12ULL)) goto mn160;
        gm_b16_8 t162 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l22 = t162;
        uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t164 != ((uint8_t)12ULL)) goto ie163;
        gm_b16_8 t165 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l23 = t165;
        {
            uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
            uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            uint8_t t170 = (!(t168 == t169));
            if (!t170) goto ie167;
            {
                uint8_t t172 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix167;
ie167:;
ix167:;
        }
        {
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                gm_l24 = t174;
            }
            {
                for (;;) {
                    uint64_t t176 = gm_l25;
                    uint64_t t177 = gm_l24;
                    uint8_t t178 = (t176 < t177);
                    if (!t178) goto lb175;
                    {
                        uint64_t t181 = gm_l25;
                        uint64_t t182 = t181;
                        uint64_t t185 = GM_LD(uint64_t, ((uint8_t *)&t182));
                        gm_l26 = t185;
                        {
                            void * t188 = gm_l0;
                            uint64_t t189 = gm_l26;
                            uint64_t t190 = (uintptr_t)t189;
                            uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                            if (t190 >= (uintptr_t)(t191)) gm_trap_index(t190, t191, gm_srcmhupam4se3h5, 194, 40);
                            gm_t88rcucg1b7uk t192 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t190 * 4));
                            uint64_t t193 = gm_l26;
                            uint64_t t194 = (uintptr_t)t193;
                            uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_srcmhupam4se3h5, 194, 46);
                            gm_t88rcucg1b7uk t196 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t194 * 4));
                            uint8_t t197 = gm_f_provably_distinct_q4efcr7(t188, GM_CONV(gm_csguankko4lmhd, t192), GM_CONV(gm_csguankko4lmhd, t196));
                            if (!t197) goto ie187;
                            {
                                uint8_t t199 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix187;
ie187:;
ix187:;
                        }
                        goto me183;
mn184:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 193, 13);
me183:;
                    }
lc175:;
                    {
                        uint64_t t201 = gm_l25;
                        uint64_t r202; bool o203 = __builtin_add_overflow(t201, ((uint64_t)1ULL), &r202);
                        uint64_t t204 = r202;
                        gm_l25 = t204;
                    }
                }
lb175:;
            }
        }
        {
            uint8_t t205 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix163;
ie163:;
ix163:;
        goto me40;
mn160:;
ma47:;
        goto me40;
mn206:;
ma48:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 177, 5);
me40:;
    }
    {
        gm_t53at80pfdus3 t208 = F.l3;
        gm_t53at80pfdus3 t209 = t208;
        uint8_t t210 = GM_LD(uint8_t, ((uint8_t *)&t209));
        gm_t53at80pfdus3 t211 = F.l4;
        gm_t53at80pfdus3 t212 = t211;
        uint8_t t213 = GM_LD(uint8_t, ((uint8_t *)&t212));
        uint8_t t214 = (!(t210 == t213));
        uint8_t t215 = 0;
        F.ret = t214;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_effect_arg_4h6q4qv F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_tnpifv20irnph gm_l4; /* set */
    gm_tnpifv20irnph gm_l5; /* set */
    gm_tiilmfd16v3fc gm_l6; /* r */
    gm_tfe2ufaqq8re9 gm_l7; /* <temporary array> */
    GM_STACK_CHECK(&F, 845u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l2;
        gm_tkn42u7na3t81 t5 = gm_f_effect_param_named_h2jeoo7(t3, t4);
        gm_tkn42u7na3t81 t6 = t5;
        if (!((((uint8_t *)&t6))[4] != 0)) goto ie2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            gm_t88rcucg1b7uk t9 = gm_l3;
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
        uint64_t t13 = gm_l1;
        void * t14 = gm_l2;
        gm_teseu4tn5o422 t15;
        gm_f_named_effect_set_s5gn1lr((void *)&t15, (void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teseu4tn5o422 t16 = t15;
        if (!((((uint8_t *)&t16))[16] != 0)) goto ie11;
        gm_tnpifv20irnph t17 = GM_LD(gm_tnpifv20irnph, ((uint8_t *)&t16));
        gm_l4 = t17;
        {
            void * t19 = gm_l0;
            void * t20 = (void *)(((uint8_t *)t19) + 8);
            gm_tnpifv20irnph t21 = gm_l4;
            gm_t53at80pfdus3 t22;
            memset(&t22, 0, sizeof t22);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)24ULL));
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t22 + 8), t21);
            gm_t88rcucg1b7uk t23 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t20, &t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t26 = gm_l2;
        uint8_t t27;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
        if (t30 != ((uint8_t)13ULL)) goto mn29;
        uint8_t t31 = GM_LD(uint8_t, ((((((uint8_t *)t26) + 8) + 8) + 0) + 0));
        if (t31 != ((uint8_t)0ULL)) goto mn29;
        uint8_t t32 = GM_LD(uint8_t, (((((((uint8_t *)t26) + 8) + 8) + 0) + 1) + 0));
        if (t32 != ((uint8_t)0ULL)) goto mn29;
        t27 = ((uint8_t)1);
        goto me28;
mn29:;
        t27 = ((uint8_t)0);
        goto me28;
mn33:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 395, 8);
me28:;
        if (!t27) goto ie25;
        {
            void * t35 = gm_l0;
            uint64_t t36 = gm_l1;
            void * t37 = gm_l2;
            gm_b16_8 t38 = gm_f_effect_terms_bphj7th((void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t39 = t38;
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_tnpifv20irnph t41 = gm_f_resolve_effect_set_o7rc5od((void *)NULL, cx, NULL, t35, t36, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t41;
        }
        {
            void * t42 = gm_l0;
            void * t43 = (void *)(((uint8_t *)t42) + 8);
            gm_tnpifv20irnph t44 = gm_l5;
            gm_t53at80pfdus3 t45;
            memset(&t45, 0, sizeof t45);
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)24ULL));
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t45 + 8), t44);
            gm_t88rcucg1b7uk t46 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t43, &t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t47 = 0;
            F.ret = t46;
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        void * t48 = gm_l0;
        uint64_t t49 = gm_l1;
        void * t50 = gm_l2;
        gm_tqu3m2l3n8mnc t51;
        gm_f_resolve_effect_ref_bggirs2((void *)&t51, (void *)NULL, cx, NULL, t48, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqu3m2l3n8mnc t52 = t51;
        gm_tiilmfd16v3fc t53;
        if (((((uint8_t *)&t52))[24] != 0)) {
            gm_tiilmfd16v3fc t54 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t52) + 0));
            t53 = t54;
        } else {
            void * t55 = gm_l0;
            void * t56 = (void *)(((uint8_t *)t55) + 8);
            gm_t88rcucg1b7uk t57 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        gm_l6 = t53;
    }
    {
    }
    {
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 8);
        {
            gm_tiilmfd16v3fc t62 = gm_l6;
            GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_tfe2ufaqq8re9 *)((uint8_t *)&gm_l7)) + 0, t62);
        }
        void * t63 = ((uint8_t *)&gm_l7);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcmhupam4se3h5, 400, 51);
        gm_b16_8 t64;
        GM_ST(void *, (uint8_t *)&t64, (void *)(((uint8_t *)t63 + (uintptr_t)0 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t65 = t64;
        gm_tnpifv20irnph t66 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t67;
        memset(&t67, 0, sizeof t67);
        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)24ULL));
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t67 + 8), t66);
        gm_t88rcucg1b7uk t68 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t60, &t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t69 = 0;
        F.ret = t68;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_resolve_effect_list_uhjlh6o F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* list_ */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 278u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_qs7fohu((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l4 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = gm_l4;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l6 = t14;
                    {
                        void * t16 = (void *)((uint8_t *)&F.l3);
                        uint64_t t17 = gm_l6;
                        uint64_t t18 = (uintptr_t)t17;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srcmhupam4se3h5, 254, 41);
                        void * t20 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t18 * 88);
                        gm_tftb1odc739t5 t21 = gm_f_push_57kepgg(t16, t20);
                        gm_tftb1odc739t5 t22 = t21;
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcmhupam4se3h5, 254, 29);
                        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 254, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l5;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l5 = t28;
                }
            }
lb4:;
        }
    }
    {
        void * t29 = gm_l0;
        uint64_t t30 = gm_l1;
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t32 = t31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_tnpifv20irnph t34 = gm_f_resolve_effect_set_o7rc5od((void *)NULL, cx, NULL, t29, t30, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = 0;
        F.ret = t34;
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
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_effect_ref_bggirs2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* p */
    void * gm_l4; /* callee */
    gm_b16_8 gm_l5; /* args */
    gm_tiilmfd16v3fc gm_l6; /* base */
    gm_b16_8 gm_l7; /* params */
    uint64_t gm_l8; /* required */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint32_t gm_l12; /* p */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* p */
    void * gm_l18; /* a */
    gm_t08olq97rscn3 gm_l20; /* id */
    uint64_t gm_l21; /* d */
    uint32_t gm_l22; /* eff */
    gm_tiilmfd16v3fc gm_l23; /* r */
    void * gm_l24; /* base */
    gm_t08olq97rscn3 gm_l25; /* name */
    uint64_t gm_l26; /* m */
    uint64_t gm_l27; /* d */
    uint32_t gm_l28; /* eff */
    gm_tiilmfd16v3fc gm_l29; /* r */
    GM_STACK_CHECK(&F, 4608u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l2;
        gm_tkn42u7na3t81 t5 = gm_f_effect_param_named_h2jeoo7(t3, t4);
        gm_tkn42u7na3t81 t6 = t5;
        if (!((((uint8_t *)&t6))[4] != 0)) goto ie2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            void * t9 = gm_l0;
            void * t10 = gm_l2;
            gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 72));
            void * t12 = gm_l0;
            gm_t88rcucg1b7uk t13 = gm_l3;
            gm_b16_8 t14 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t15;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
            gm_tabb710d9mq2e t16;
            gm_f_format_uvukia4((void *)&t16, (void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t17 = t16;
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srcmhupam4se3h5, 293, 25);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_b16_8 t19 = t18;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_b0_1 t21 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t9, t11, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tqu3m2l3n8mnc t22;
            memset(&t22, 0, sizeof t22);
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t25 = gm_l2;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
        if (t26 != ((uint8_t)19ULL)) goto ie24;
        void * t27 = GM_LD(void *, (((((uint8_t *)t25) + 8) + 8) + 0));
        gm_l4 = t27;
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((((uint8_t *)t25) + 8) + 8) + 8));
        gm_l5 = t28;
        {
            void * t30 = gm_l0;
            uint64_t t31 = gm_l1;
            void * t32 = gm_l4;
            gm_tqu3m2l3n8mnc t33;
            gm_f_resolve_effect_ref_bggirs2((void *)&t33, (void *)NULL, cx, NULL, t30, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqu3m2l3n8mnc t34 = t33;
            gm_tiilmfd16v3fc t35;
            if (((((uint8_t *)&t34))[24] != 0)) {
                gm_tiilmfd16v3fc t36 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t34) + 0));
                t35 = t36;
            } else {
                gm_tqu3m2l3n8mnc t37;
                memset(&t37, 0, sizeof t37);
                uint8_t t38 = 0;
                F.ret = t37;
                goto gm_fret;
            }
            gm_l6 = t35;
        }
        {
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            uint64_t t40 = (uint64_t)t39;
            uint64_t t41 = (uintptr_t)t40;
            void * t42 = gm_l0;
            uint64_t t43 = GM_LD(uintptr_t, ((((uint8_t *)t42) + 1048) + 0) + 8);
            if (t41 >= (uintptr_t)(t43)) gm_trap_index(t41, t43, gm_srcmhupam4se3h5, 299, 19);
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t42) + 1048) + 0)) + t41 * 56) + 24));
            gm_l7 = t44;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t46 = gm_l7;
                gm_b16_8 t47 = t46;
                gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                gm_l9 = t48;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t50 = gm_l10;
                    uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t52 = (t50 < t51);
                    if (!t52) goto lb49;
                    {
                        uint64_t t55 = gm_l10;
                        uint64_t t56 = gm_l10;
                        uint64_t t57 = (uintptr_t)t56;
                        uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        uint32_t t59 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t57 * 4));
                        gm_tn68el8d14vku t60;
                        GM_ST(uint32_t, (uint8_t *)&t60 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t60 + 0), t55);
                        GM_ST(uint32_t, ((uint8_t *)&t60 + 8), t59);
                        gm_tn68el8d14vku t61 = t60;
                        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&t61) + 0));
                        gm_l11 = t64;
                        uint32_t t65 = GM_LD(uint32_t, (((uint8_t *)&t61) + 8));
                        gm_l12 = t65;
                        {
                            uint32_t t68 = gm_l12;
                            uint64_t t69 = (uint64_t)t68;
                            uint64_t t70 = (uintptr_t)t69;
                            void * t71 = gm_l0;
                            uint64_t t72 = GM_LD(uintptr_t, ((((uint8_t *)t71) + 1128) + 0) + 8);
                            if (t70 >= (uintptr_t)(t72)) gm_trap_index(t70, t72, gm_srcmhupam4se3h5, 303, 16);
                            uint8_t t73 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t71) + 1128) + 0)) + t70 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                            if (!t73) goto ie67;
                            {
                                uint64_t t75 = gm_l11;
                                uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                                if (o77) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 303, 75);
                                uint64_t t78 = r76;
                                gm_l8 = t78;
                            }
                            goto ix67;
ie67:;
ix67:;
                        }
                        goto me62;
mn63:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 302, 9);
me62:;
                    }
lc49:;
                    {
                        uint64_t t80 = gm_l10;
                        uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                        uint64_t t83 = r81;
                        gm_l10 = t83;
                    }
                }
lb49:;
            }
        }
        {
            uint8_t t85 = 0;
            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint8_t t88 = (t86 > t87);
            if (!t88) {
                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint64_t t90 = gm_l8;
                uint8_t t91 = (t89 < t90);
                t85 = t91;
            } else t85 = 1;
            if (!t85) goto ie84;
            {
                void * t93 = gm_l0;
                void * t94 = gm_l2;
                gm_tl874t2atnlh3 t95 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t94) + 72));
                gm_t3877pk7lv748 t96;
                void * t97 = gm_l0;
                void * t98 = (void *)(((uint8_t *)t97) + 8);
                uint32_t t99 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                gm_b16_8 t100 = gm_f_effect_name_prpp0ip(t98, t99);
                GM_ST(gm_b16_8, (uint8_t *)&t96 + 0, t100);
                uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                GM_ST(uint64_t, (uint8_t *)&t96 + 16, t101);
                gm_tabb710d9mq2e t102;
                gm_f_format_n6tblha((void *)&t102, (void *)NULL, cx, NULL, &t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t103 = t102;
                if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 8, gm_srcmhupam4se3h5, 306, 29);
                gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&t103) + 8));
                gm_b16_8 t105 = t104;
                gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                gm_b0_1 t107 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t93, t95, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tqu3m2l3n8mnc t108;
                memset(&t108, 0, sizeof t108);
                uint8_t t109 = 0;
                F.ret = t108;
                goto gm_fret;
            }
            goto ix84;
ie84:;
ix84:;
        }
        {
            gm_f_list_vmvl81b((void *)&F.l13, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t111 = gm_l7;
                gm_b16_8 t112 = t111;
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                gm_l14 = t113;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t115 = gm_l15;
                    uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t117 = (t115 < t116);
                    if (!t117) goto lb114;
                    {
                        uint64_t t120 = gm_l15;
                        uint64_t t121 = gm_l15;
                        uint64_t t122 = (uintptr_t)t121;
                        uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        uint32_t t124 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t122 * 4));
                        gm_tn68el8d14vku t125;
                        GM_ST(uint32_t, (uint8_t *)&t125 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t125 + 0), t120);
                        GM_ST(uint32_t, ((uint8_t *)&t125 + 8), t124);
                        gm_tn68el8d14vku t126 = t125;
                        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&t126) + 0));
                        gm_l16 = t129;
                        uint32_t t130 = GM_LD(uint32_t, (((uint8_t *)&t126) + 8));
                        gm_l17 = t130;
                        {
                            uint64_t t133 = gm_l16;
                            uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            uint8_t t135 = (t133 >= t134);
                            if (!t135) goto ie132;
                            {
                                void * t137 = (void *)((uint8_t *)&F.l13);
                                void * t138 = gm_l0;
                                void * t139 = (void *)(((uint8_t *)t138) + 8);
                                gm_tnpifv20irnph t140 = gm_f_empty_effects_r0iv3pv();
                                gm_t53at80pfdus3 t141;
                                memset(&t141, 0, sizeof t141);
                                GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)24ULL));
                                GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t141 + 8), t140);
                                gm_t88rcucg1b7uk t142 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t139, &t141);
                                if (gm_unw.kind) {
                                    goto gm_ll118;
                                }
                                gm_toa2vhe3l4hfr t143;
                                memset(&t143, 0, sizeof t143);
                                GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t143 + 8), t142);
                                gm_tftb1odc739t5 t144 = gm_f_push_s5vr2a2(t137, &t143);
                                gm_tftb1odc739t5 t145 = t144;
                                if (((((uint8_t *)&t145))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t145) + 1, gm_srcmhupam4se3h5, 312, 17);
                                gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 1));
                            }
                            {
                                goto lc114;
                            }
                            goto ix132;
ie132:;
ix132:;
                        }
                        {
                            uint64_t t147 = gm_l16;
                            uint64_t t148 = (uintptr_t)t147;
                            uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            if (t148 >= (uintptr_t)(t149)) gm_trap_index(t148, t149, gm_srcmhupam4se3h5, 315, 19);
                            void * t150 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t148 * 144);
                            gm_l18 = t150;
                        }
                        {
                            void * t152 = gm_l18;
                            uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)t152) + 0) + 0));
                            if (t153 != ((uint8_t)0ULL)) goto ie151;
                            gm_tc3oht4ksth9d t154 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t152) + 0) + 8));
                            F.l19 = t154;
                            {
                                uint32_t t157 = gm_l17;
                                uint64_t t158 = (uint64_t)t157;
                                uint64_t t159 = (uintptr_t)t158;
                                void * t160 = gm_l0;
                                uint64_t t161 = GM_LD(uintptr_t, ((((uint8_t *)t160) + 1128) + 0) + 8);
                                if (t159 >= (uintptr_t)(t161)) gm_trap_index(t159, t161, gm_srcmhupam4se3h5, 320, 16);
                                uint8_t t162 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t160) + 1128) + 0)) + t159 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                                if (!t162) goto ie156;
                                {
                                    void * t164 = (void *)((uint8_t *)&F.l13);
                                    void * t165 = gm_l0;
                                    uint64_t t166 = gm_l1;
                                    void * t167 = (void *)((uint8_t *)&F.l19);
                                    gm_t88rcucg1b7uk t168 = gm_f_resolve_effect_arg_4h6q4qv((void *)NULL, cx, NULL, t165, t166, t167);
                                    if (gm_unw.kind) {
                                        goto gm_ll118;
                                    }
                                    gm_toa2vhe3l4hfr t169;
                                    memset(&t169, 0, sizeof t169);
                                    GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t169 + 8), t168);
                                    gm_tftb1odc739t5 t170 = gm_f_push_s5vr2a2(t164, &t169);
                                    gm_tftb1odc739t5 t171 = t170;
                                    if (((((uint8_t *)&t171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t171) + 1, gm_srcmhupam4se3h5, 321, 17);
                                    gm_b0_1 t172 = GM_LD(gm_b0_1, (((uint8_t *)&t171) + 1));
                                }
                                goto ix156;
ie156:;
                                {
                                    void * t174 = (void *)((uint8_t *)&F.l13);
                                    void * t175 = gm_l0;
                                    uint64_t t176 = gm_l1;
                                    void * t177 = (void *)((uint8_t *)&F.l19);
                                    gm_t88rcucg1b7uk t178 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t175, t176, t177);
                                    if (gm_unw.kind) {
                                        goto gm_ll118;
                                    }
                                    gm_toa2vhe3l4hfr t179;
                                    memset(&t179, 0, sizeof t179);
                                    GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t179 + 8), t178);
                                    gm_tftb1odc739t5 t180 = gm_f_push_s5vr2a2(t174, &t179);
                                    gm_tftb1odc739t5 t181 = t180;
                                    if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_srcmhupam4se3h5, 323, 17);
                                    gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
                                }
ix156:;
                            }
                            goto ix151;
ie151:;
                            {
                                {
                                    void * t185 = gm_l0;
                                    void * t186 = gm_l18;
                                    gm_tl874t2atnlh3 t187 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t186) + 128));
                                    gm_b16_8 t188;
                                    GM_ST(void *, (uint8_t *)&t188, (void *)(gm_spskd7qirpoe3));
                                    GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(24));
                                    gm_b0_1 t189 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t185, t187, t188);
                                    if (gm_unw.kind) {
                                        goto gm_ll118;
                                    }
                                }
                                {
                                    gm_tqu3m2l3n8mnc t190;
                                    memset(&t190, 0, sizeof t190);
                                    uint8_t t191 = 0;
                                    F.ret = t190;
                                    goto gm_fret;
                                }
                            }
ix151:;
                        }
                        goto me127;
mn128:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 310, 9);
me127:;
                    }
lc114:;
                    {
                        uint64_t t193 = gm_l15;
                        uint64_t r194; bool o195 = __builtin_add_overflow(t193, ((uint64_t)1ULL), &r194);
                        uint64_t t196 = r194;
                        gm_l15 = t196;
                    }
                }
gm_ll118:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb114; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc114; }
                }
                goto gm_lad;
lb114:;
            }
        }
        {
            gm_tiilmfd16v3fc t197;
            memset((uint8_t *)&t197 + 4, 0, 4);
            uint32_t t198 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            GM_ST(uint32_t, (uint8_t *)&t197 + 0, t198);
            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t200 = t199;
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
            GM_ST(gm_b16_8, (uint8_t *)&t197 + 8, t201);
            gm_tqu3m2l3n8mnc t202;
            GM_ST(uint8_t, (uint8_t *)&t202 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t202 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t202 + 28, 0);
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t202), t197);
            ((uint8_t *)&t202)[24] = 1;
            uint8_t t203 = 0;
            F.ret = t202;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t205 = gm_l2;
        uint8_t t206 = GM_LD(uint8_t, ((((uint8_t *)t205) + 8) + 0));
        if (t206 != ((uint8_t)6ULL)) goto ie204;
        gm_t08olq97rscn3 t207 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t205) + 8) + 8));
        gm_l20 = t207;
        {
            void * t209 = gm_l0;
            uint64_t t210 = gm_l1;
            gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
            gm_tf5srh4mp4b3i t212 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t209, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t213 = t212;
            uint64_t t214;
            if (((((uint8_t *)&t213))[8] != 0)) {
                uint64_t t215 = GM_LD(uint64_t, (((uint8_t *)&t213) + 0));
                t214 = t215;
            } else {
                {
                    void * t217 = gm_l0;
                    void * t218 = gm_l2;
                    gm_tl874t2atnlh3 t219 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t218) + 72));
                    gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                    gm_tnpifv20irnph t221;
                    GM_ST(gm_b16_8, ((uint8_t *)&t221 + 0), t220);
                    gm_tabb710d9mq2e t222;
                    gm_f_format_kjp7rmf((void *)&t222, (void *)NULL, cx, NULL, t221);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t223 = t222;
                    if (((((uint8_t *)&t223))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t223) + 8, gm_srcmhupam4se3h5, 330, 29);
                    gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&t223) + 8));
                    gm_b16_8 t225 = t224;
                    gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                    gm_b0_1 t227 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t217, t219, t226);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_tqu3m2l3n8mnc t228;
                memset(&t228, 0, sizeof t228);
                uint8_t t229 = 0;
                F.ret = t228;
                goto gm_fret;
            }
            gm_l21 = t214;
        }
        {
            void * t230 = gm_l0;
            uint64_t t231 = gm_l21;
            gm_t6qooom8qa2do t232;
            gm_f_resolve_decl_pus042v((void *)&t232, (void *)NULL, cx, NULL, t230, t231);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t233 = t232;
            uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&t233) + 0));
            switch (t241) { case ((uint8_t)0ULL): goto ma239; case ((uint8_t)1ULL): goto ma239; case ((uint8_t)2ULL): goto ma235; case ((uint8_t)3ULL): goto ma236; case ((uint8_t)4ULL): goto ma238; case ((uint8_t)5ULL): goto ma239; case ((uint8_t)6ULL): goto ma239; case ((uint8_t)7ULL): goto ma239; case ((uint8_t)8ULL): goto ma239; case ((uint8_t)9ULL): goto ma239; case ((uint8_t)10ULL): goto ma239; case ((uint8_t)11ULL): goto ma237; default: goto ma239; }
ma235:;
            uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&t233) + 0));
            if (t243 != ((uint8_t)2ULL)) goto mn242;
            uint32_t t244 = GM_LD(uint32_t, (((uint8_t *)&t233) + 8));
            gm_l22 = t244;
            void * t245 = gm_l0;
            uint32_t t246 = gm_l22;
            gm_tiilmfd16v3fc t247;
            gm_f_bare_effect_ref_a5ofmji((void *)&t247, (void *)NULL, cx, NULL, t245, t246);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqu3m2l3n8mnc t248;
            GM_ST(uint8_t, (uint8_t *)&t248 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t248 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t248 + 28, 0);
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t248), t247);
            ((uint8_t *)&t248)[24] = 1;
            uint8_t t249 = 0;
            F.ret = t248;
            goto gm_fret;
            goto me234;
mn242:;
ma236:;
            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&t233) + 0));
            if (t251 != ((uint8_t)3ULL)) goto mn250;
            gm_tiilmfd16v3fc t252 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t233) + 8));
            gm_l23 = t252;
            gm_tiilmfd16v3fc t253 = gm_l23;
            gm_tqu3m2l3n8mnc t254;
            GM_ST(uint8_t, (uint8_t *)&t254 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t254 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t254 + 28, 0);
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t254), t253);
            ((uint8_t *)&t254)[24] = 1;
            uint8_t t255 = 0;
            F.ret = t254;
            goto gm_fret;
            goto me234;
mn250:;
ma237:;
            uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)&t233) + 0));
            if (t257 != ((uint8_t)11ULL)) goto mn256;
            gm_tqu3m2l3n8mnc t258;
            memset(&t258, 0, sizeof t258);
            uint8_t t259 = 0;
            F.ret = t258;
            goto gm_fret;
            goto me234;
mn256:;
ma238:;
            uint8_t t261 = GM_LD(uint8_t, (((uint8_t *)&t233) + 0));
            if (t261 != ((uint8_t)4ULL)) goto mn260;
            {
                void * t263 = gm_l0;
                void * t264 = gm_l2;
                gm_tl874t2atnlh3 t265 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t264) + 72));
                gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                gm_tnpifv20irnph t267;
                GM_ST(gm_b16_8, ((uint8_t *)&t267 + 0), t266);
                gm_tabb710d9mq2e t268;
                gm_f_format_37dbjte((void *)&t268, (void *)NULL, cx, NULL, t267);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t269 = t268;
                if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 8, gm_srcmhupam4se3h5, 338, 33);
                gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&t269) + 8));
                gm_b16_8 t271 = t270;
                gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&t271));
                gm_b0_1 t273 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t263, t265, t272);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tqu3m2l3n8mnc t274;
                memset(&t274, 0, sizeof t274);
                uint8_t t275 = 0;
                F.ret = t274;
                goto gm_fret;
            }
            goto me234;
mn260:;
ma239:;
            {
                void * t278 = gm_l0;
                void * t279 = gm_l2;
                gm_tl874t2atnlh3 t280 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t279) + 72));
                gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                gm_tnpifv20irnph t282;
                GM_ST(gm_b16_8, ((uint8_t *)&t282 + 0), t281);
                gm_tabb710d9mq2e t283;
                gm_f_format_vilpuor((void *)&t283, (void *)NULL, cx, NULL, t282);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t284 = t283;
                if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 8, gm_srcmhupam4se3h5, 342, 33);
                gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&t284) + 8));
                gm_b16_8 t286 = t285;
                gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
                gm_b0_1 t288 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t278, t280, t287);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tqu3m2l3n8mnc t289;
                memset(&t289, 0, sizeof t289);
                uint8_t t290 = 0;
                F.ret = t289;
                goto gm_fret;
            }
            goto me234;
mn276:;
ma240:;
            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 333, 9);
me234:;
        }
        goto ix204;
ie204:;
ix204:;
    }
    {
        void * t292 = gm_l2;
        uint8_t t293 = GM_LD(uint8_t, ((((uint8_t *)t292) + 8) + 0));
        if (t293 != ((uint8_t)22ULL)) goto ie291;
        void * t294 = GM_LD(void *, (((((uint8_t *)t292) + 8) + 8) + 0));
        gm_l24 = t294;
        gm_t08olq97rscn3 t295 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t292) + 8) + 8) + 8));
        gm_l25 = t295;
        {
            void * t298 = gm_l0;
            uint64_t t299 = gm_l1;
            void * t300 = gm_l24;
            gm_tf5srh4mp4b3i t301 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t298, t299, t300);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t302 = t301;
            if (!((((uint8_t *)&t302))[8] != 0)) goto ie297;
            uint64_t t303 = GM_LD(uint64_t, ((uint8_t *)&t302));
            gm_l26 = t303;
            {
                void * t305 = gm_l0;
                uint64_t t306 = gm_l26;
                uint64_t t307 = gm_l1;
                gm_tf5srh4mp4b3i t308 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t305, t306, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l25)), t307);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tf5srh4mp4b3i t309 = t308;
                uint64_t t310;
                if (((((uint8_t *)&t309))[8] != 0)) {
                    uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&t309) + 0));
                    t310 = t311;
                } else {
                    gm_tqu3m2l3n8mnc t312;
                    memset(&t312, 0, sizeof t312);
                    uint8_t t313 = 0;
                    F.ret = t312;
                    goto gm_fret;
                }
                gm_l27 = t310;
            }
            {
                void * t314 = gm_l0;
                uint64_t t315 = gm_l27;
                gm_t6qooom8qa2do t316;
                gm_f_resolve_decl_pus042v((void *)&t316, (void *)NULL, cx, NULL, t314, t315);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t317 = t316;
                uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&t317) + 0));
                switch (t324) { case ((uint8_t)0ULL): goto ma322; case ((uint8_t)1ULL): goto ma322; case ((uint8_t)2ULL): goto ma319; case ((uint8_t)3ULL): goto ma320; case ((uint8_t)4ULL): goto ma322; case ((uint8_t)5ULL): goto ma322; case ((uint8_t)6ULL): goto ma322; case ((uint8_t)7ULL): goto ma322; case ((uint8_t)8ULL): goto ma322; case ((uint8_t)9ULL): goto ma322; case ((uint8_t)10ULL): goto ma322; case ((uint8_t)11ULL): goto ma321; default: goto ma322; }
ma319:;
                uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)&t317) + 0));
                if (t326 != ((uint8_t)2ULL)) goto mn325;
                uint32_t t327 = GM_LD(uint32_t, (((uint8_t *)&t317) + 8));
                gm_l28 = t327;
                void * t328 = gm_l0;
                uint32_t t329 = gm_l28;
                gm_tiilmfd16v3fc t330;
                gm_f_bare_effect_ref_a5ofmji((void *)&t330, (void *)NULL, cx, NULL, t328, t329);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqu3m2l3n8mnc t331;
                GM_ST(uint8_t, (uint8_t *)&t331 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t331 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t331 + 28, 0);
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t331), t330);
                ((uint8_t *)&t331)[24] = 1;
                uint8_t t332 = 0;
                F.ret = t331;
                goto gm_fret;
                goto me318;
mn325:;
ma320:;
                uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&t317) + 0));
                if (t334 != ((uint8_t)3ULL)) goto mn333;
                gm_tiilmfd16v3fc t335 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t317) + 8));
                gm_l29 = t335;
                gm_tiilmfd16v3fc t336 = gm_l29;
                gm_tqu3m2l3n8mnc t337;
                GM_ST(uint8_t, (uint8_t *)&t337 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t337 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t337 + 28, 0);
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t337), t336);
                ((uint8_t *)&t337)[24] = 1;
                uint8_t t338 = 0;
                F.ret = t337;
                goto gm_fret;
                goto me318;
mn333:;
ma321:;
                uint8_t t340 = GM_LD(uint8_t, (((uint8_t *)&t317) + 0));
                if (t340 != ((uint8_t)11ULL)) goto mn339;
                gm_tqu3m2l3n8mnc t341;
                memset(&t341, 0, sizeof t341);
                uint8_t t342 = 0;
                F.ret = t341;
                goto gm_fret;
                goto me318;
mn339:;
ma322:;
                {
                    void * t345 = gm_l0;
                    void * t346 = gm_l2;
                    gm_tl874t2atnlh3 t347 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t346) + 72));
                    gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 0));
                    gm_tnpifv20irnph t349;
                    GM_ST(gm_b16_8, ((uint8_t *)&t349 + 0), t348);
                    gm_tabb710d9mq2e t350;
                    gm_f_format_vilpuor((void *)&t350, (void *)NULL, cx, NULL, t349);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t351 = t350;
                    if (((((uint8_t *)&t351))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t351) + 8, gm_srcmhupam4se3h5, 356, 37);
                    gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)&t351) + 8));
                    gm_b16_8 t353 = t352;
                    gm_b16_8 t354 = GM_LD(gm_b16_8, ((uint8_t *)&t353));
                    gm_b0_1 t355 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t345, t347, t354);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tqu3m2l3n8mnc t356;
                    memset(&t356, 0, sizeof t356);
                    uint8_t t357 = 0;
                    F.ret = t356;
                    goto gm_fret;
                }
                goto me318;
mn343:;
ma323:;
                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 351, 13);
me318:;
            }
            goto ix297;
ie297:;
ix297:;
        }
        {
            void * t359 = gm_l0;
            uint64_t t360 = gm_l1;
            void * t361 = gm_l24;
            uint8_t t362 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t359, t360, t361);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t362) goto ie358;
            {
                gm_tqu3m2l3n8mnc t364;
                memset(&t364, 0, sizeof t364);
                uint8_t t365 = 0;
                F.ret = t364;
                goto gm_fret;
            }
            goto ix358;
ie358:;
ix358:;
        }
        goto ix291;
ie291:;
ix291:;
    }
    {
        void * t366 = gm_l0;
        void * t367 = gm_l2;
        gm_tl874t2atnlh3 t368 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t367) + 72));
        gm_b16_8 t369;
        GM_ST(void *, (uint8_t *)&t369, (void *)(gm_sq5p8t05ccn5h));
        GM_ST(uintptr_t, (uint8_t *)&t369 + 8, (uintptr_t)(23));
        gm_b0_1 t370 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t366, t368, t369);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tqu3m2l3n8mnc t371;
        memset(&t371, 0, sizeof t371);
        uint8_t t372 = 0;
        F.ret = t371;
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
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_set_o7rc5od(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_resolve_effect_set_o7rc5od F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* terms */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* e */
    gm_t88rcucg1b7uk gm_l7; /* p */
    gm_tnpifv20irnph gm_l8; /* set */
    gm_tiilmfd16v3fc gm_l9; /* r */
    GM_STACK_CHECK(&F, 618u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_084fcd6((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
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
                    void * t13 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 8));
                    void * t14 = t13;
                    void * t17 = GM_LD(void *, ((uint8_t *)&t14));
                    gm_l6 = t17;
                    {
                        void * t20 = gm_l0;
                        void * t21 = gm_l6;
                        gm_tkn42u7na3t81 t22 = gm_f_effect_param_named_h2jeoo7(t20, t21);
                        gm_tkn42u7na3t81 t23 = t22;
                        if (!((((uint8_t *)&t23))[4] != 0)) goto ie19;
                        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t23));
                        gm_l7 = t24;
                        {
                            void * t26 = (void *)((uint8_t *)&F.l3);
                            gm_t88rcucg1b7uk t27 = gm_l7;
                            gm_tiilmfd16v3fc t28;
                            gm_f_splice_tj8iedl((void *)&t28, (void *)NULL, cx, NULL, t27);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tftb1odc739t5 t29 = gm_f_push_d1r4h19(t26, &t28);
                            gm_tftb1odc739t5 t30 = t29;
                            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcmhupam4se3h5, 239, 13);
                            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                        }
                        {
                            goto lc4;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    {
                        void * t33 = gm_l0;
                        uint64_t t34 = gm_l1;
                        void * t35 = gm_l6;
                        gm_teseu4tn5o422 t36;
                        gm_f_named_effect_set_s5gn1lr((void *)&t36, (void *)NULL, cx, NULL, t33, t34, t35);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_teseu4tn5o422 t37 = t36;
                        if (!((((uint8_t *)&t37))[16] != 0)) goto ie32;
                        gm_tnpifv20irnph t38 = GM_LD(gm_tnpifv20irnph, ((uint8_t *)&t37));
                        gm_l8 = t38;
                        {
                            void * t40 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                            gm_tftb1odc739t5 t42 = gm_f_extend_74ac4hb(t40, t41);
                            gm_tftb1odc739t5 t43 = t42;
                            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcmhupam4se3h5, 243, 13);
                            gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                        }
                        {
                            goto lc4;
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    {
                        void * t46 = gm_l0;
                        uint64_t t47 = gm_l1;
                        void * t48 = gm_l6;
                        gm_tqu3m2l3n8mnc t49;
                        gm_f_resolve_effect_ref_bggirs2((void *)&t49, (void *)NULL, cx, NULL, t46, t47, t48);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tqu3m2l3n8mnc t50 = t49;
                        if (!((((uint8_t *)&t50))[24] != 0)) goto ie45;
                        gm_tiilmfd16v3fc t51 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&t50));
                        gm_l9 = t51;
                        {
                            void * t53 = (void *)((uint8_t *)&F.l3);
                            gm_tftb1odc739t5 t54 = gm_f_push_d1r4h19(t53, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l9)));
                            gm_tftb1odc739t5 t55 = t54;
                            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srcmhupam4se3h5, 246, 51);
                            gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 237, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t58 = gm_l5;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l5 = t61;
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
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t63 = t62;
        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
        gm_tnpifv20irnph t65 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t66 = 0;
        F.ret = t65;
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
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4) {
    struct gm_fr_resolve_fn_sig_pamuhve F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* sig */
    uint8_t gm_l3; /* literal */
    gm_b16_8 gm_l4; /* where_ */
    uint8_t gm_l5; /* ok */
    uint8_t gm_l8; /* seen_default */
    gm_t66j1im9ip128 gm_l9; /* mark */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    void * gm_l13; /* ty_expr */
    gm_t88rcucg1b7uk gm_l14; /* ty */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t08olq97rscn3 gm_l17; /* n */
    uint32_t gm_l18; /* pid */
    void * gm_l19; /* ty_expr */
    uint8_t gm_l20; /* is_effects */
    uint8_t gm_l21; /* is_type */
    gm_t08olq97rscn3 gm_l22; /* i */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_t08olq97rscn3 gm_l25; /* n */
    uint32_t gm_l26; /* pid */
    gm_t88rcucg1b7uk gm_l27; /* ty */
    uint8_t gm_l28; /* rest */
    gm_b16_8 gm_l29; /* names */
    uint8_t gm_l30; /* ok_ty */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_t08olq97rscn3 gm_l33; /* n */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* r */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_t88rcucg1b7uk gm_l41; /* ret */
    void * gm_l42; /* r */
    gm_tnpifv20irnph gm_l43; /* effects */
    gm_b16_8 gm_l44; /* list_ */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    GM_STACK_CHECK(&F, 4431u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_l5 = ((uint8_t)1);
    }
    {
        gm_f_list_af74s0q((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l8 = ((uint8_t)0);
    }
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 1768));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 31, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 1768), t7);
    }
    {
        void * t8 = gm_l0;
        gm_f_scope_mark_i3b2mvo((void *)&gm_l9, t8);
    }
    {
        {
            void * t10 = gm_l2;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 0));
            gm_l10 = t11;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l11;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l11;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_tf2orqcrphmek t23 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t19 * 56));
                    F.l12 = t23;
                    {
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 16));
                        if (!t26) goto ie25;
                        {
                            uint8_t t29 = gm_l3;
                            uint8_t t30 = (uint8_t)!t29;
                            if (!t30) goto ie28;
                            {
                                void * t32 = gm_l0;
                                gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                gm_b16_8 t34;
                                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sshv7blujc475));
                                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(51));
                                gm_b0_1 t35 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t32, t33, t34);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            {
                                goto lc12;
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        {
                            void * t36 = GM_LD(void *, (((uint8_t *)&F.l12) + 24));
                            void * t37 = t36;
                            void * t38;
                            if ((GM_LD(void *, ((uint8_t *)&t37)) != 0)) {
                                void * t39 = GM_LD(void *, (((uint8_t *)&t37) + 0));
                                t38 = t39;
                            } else {
                                {
                                    void * t41 = gm_l0;
                                    gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                    gm_b16_8 t43;
                                    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_snnov1h58rig5));
                                    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(51));
                                    gm_b0_1 t44 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t41, t42, t43);
                                    if (gm_unw.kind) {
                                        goto gm_ll16;
                                    }
                                }
                                {
                                    gm_l5 = ((uint8_t)0);
                                }
                                goto lc12;
                            }
                            gm_l13 = t38;
                        }
                        {
                            void * t45 = gm_l0;
                            uint64_t t46 = gm_l1;
                            void * t47 = gm_l13;
                            gm_t88rcucg1b7uk t48 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t45, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                            gm_l14 = t48;
                        }
                        {
                            void * t50 = gm_l0;
                            void * t51 = (void *)(((uint8_t *)t50) + 8);
                            gm_t88rcucg1b7uk t52 = gm_l14;
                            gm_t53at80pfdus3 t53;
                            gm_f_kind_jcnt4hm((void *)&t53, t51, GM_CONV(gm_csguankko4lmhd, t52));
                            gm_t53at80pfdus3 t54 = t53;
                            uint8_t t55;
                            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
                            switch (t62) { case ((uint8_t)0ULL): goto ma60; case ((uint8_t)1ULL): goto ma60; case ((uint8_t)2ULL): goto ma60; case ((uint8_t)3ULL): goto ma60; case ((uint8_t)4ULL): goto ma60; case ((uint8_t)5ULL): goto ma60; case ((uint8_t)6ULL): goto ma59; case ((uint8_t)7ULL): goto ma60; case ((uint8_t)8ULL): goto ma60; case ((uint8_t)9ULL): goto ma60; case ((uint8_t)10ULL): goto ma60; case ((uint8_t)11ULL): goto ma60; case ((uint8_t)12ULL): goto ma60; case ((uint8_t)13ULL): goto ma60; case ((uint8_t)14ULL): goto ma60; case ((uint8_t)15ULL): goto ma57; case ((uint8_t)16ULL): goto ma60; case ((uint8_t)17ULL): goto ma60; case ((uint8_t)18ULL): goto ma60; case ((uint8_t)19ULL): goto ma60; case ((uint8_t)20ULL): goto ma60; case ((uint8_t)21ULL): goto ma60; case ((uint8_t)22ULL): goto ma60; case ((uint8_t)23ULL): goto ma60; case ((uint8_t)24ULL): goto ma60; case ((uint8_t)25ULL): goto ma58; case ((uint8_t)26ULL): goto ma60; default: goto ma60; }
ma57:;
                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
                            if (t64 != ((uint8_t)15ULL)) goto mn63;
                            t55 = ((uint8_t)1);
                            goto me56;
mn63:;
ma58:;
                            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
                            if (t66 != ((uint8_t)25ULL)) goto mn65;
                            t55 = ((uint8_t)1);
                            goto me56;
mn65:;
ma59:;
                            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
                            if (t68 != ((uint8_t)6ULL)) goto mn67;
                            t55 = ((uint8_t)1);
                            goto me56;
mn67:;
ma60:;
                            t55 = ((uint8_t)0);
                            goto me56;
mn69:;
ma61:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 47, 16);
me56:;
                            if (!t55) goto ie49;
                            {
                                void * t71 = gm_l0;
                                void * t72 = gm_l13;
                                gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t72) + 72));
                                void * t74 = gm_l0;
                                gm_t88rcucg1b7uk t75 = gm_l14;
                                gm_b16_8 t76 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t74, t75);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                                gm_tnpifv20irnph t77;
                                GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
                                gm_tabb710d9mq2e t78;
                                gm_f_format_askmr98((void *)&t78, (void *)NULL, cx, NULL, t77);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                                gm_tabb710d9mq2e t79 = t78;
                                if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 8, gm_srcmhupam4se3h5, 48, 39);
                                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
                                gm_b16_8 t81 = t80;
                                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                                gm_b0_1 t83 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t71, t73, t82);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            {
                                goto lc12;
                            }
                            goto ix49;
ie49:;
ix49:;
                        }
                        {
                            {
                                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                gm_l15 = t85;
                            }
                            {
                                gm_l16 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t87 = gm_l16;
                                    uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    uint8_t t89 = (t87 < t88);
                                    if (!t89) goto lb86;
                                    {
                                        uint64_t t92 = gm_l16;
                                        uint64_t t93 = (uintptr_t)t92;
                                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        gm_t08olq97rscn3 t97 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t93 * 32));
                                        gm_l17 = t97;
                                        {
                                            void * t99 = gm_l0;
                                            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                                            gm_t88rcucg1b7uk t101 = gm_l14;
                                            gm_tnnjeerolos0a t102;
                                            GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)2ULL));
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t102 + 4), t101);
                                            uint32_t t103 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t99, t100, t102);
                                            if (gm_unw.kind) {
                                                goto gm_ll90;
                                            }
                                            gm_l18 = t103;
                                        }
                                        {
                                            void * t104 = (void *)((uint8_t *)&F.l7);
                                            uint32_t t105 = gm_l18;
                                            gm_tftb1odc739t5 t106 = gm_f_push_im9d4ro(t104, t105);
                                            gm_tftb1odc739t5 t107 = t106;
                                            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcmhupam4se3h5, 54, 17);
                                            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                                        }
                                        {
                                            void * t109 = gm_l0;
                                            void * t110 = (void *)(((uint8_t *)t109) + 1528);
                                            gm_tommf4j9upomc t111;
                                            memset((uint8_t *)&t111 + 20, 0, 4);
                                            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t111 + 0, t112);
                                            uint32_t t113 = gm_l18;
                                            GM_ST(uint32_t, (uint8_t *)&t111 + 16, t113);
                                            gm_tftb1odc739t5 t114 = gm_f_push_sp4si0f(t110, &t111);
                                            gm_tftb1odc739t5 t115 = t114;
                                            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcmhupam4se3h5, 55, 17);
                                            gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
                                        }
                                        {
                                            void * t117 = (void *)((uint8_t *)&F.l6);
                                            gm_t5v0c8ia6a5mo t118;
                                            memset((uint8_t *)&t118 + 49, 0, 7);
                                            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t118 + 0, t119);
                                            gm_t88rcucg1b7uk t120 = gm_l14;
                                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t118 + 16, t120);
                                            gm_tl874t2atnlh3 t121 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l17) + 16));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t118 + 20, t121);
                                            void * t122 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                                            GM_ST(void *, (uint8_t *)&t118 + 32, t122);
                                            uint32_t t123 = gm_l18;
                                            gm_t9805qtonbsag t124;
                                            GM_ST(uint8_t, (uint8_t *)&t124 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t124 + 6, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t124), t123);
                                            ((uint8_t *)&t124)[4] = 1;
                                            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t118 + 40, t124);
                                            GM_ST(uint8_t, (uint8_t *)&t118 + 48, ((uint8_t)0));
                                            gm_tftb1odc739t5 t125 = gm_f_push_o8u01sh(t117, &t118);
                                            gm_tftb1odc739t5 t126 = t125;
                                            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_srcmhupam4se3h5, 56, 17);
                                            gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                                        }
                                        goto me95;
mn96:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 52, 13);
me95:;
                                    }
lc86:;
                                    {
                                        uint64_t t129 = gm_l16;
                                        uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                                        uint64_t t132 = r130;
                                        gm_l16 = t132;
                                    }
                                }
gm_ll90:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb86; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc86; }
                                }
                                goto gm_ll16;
lb86:;
                            }
                        }
                        {
                            goto lc12;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    {
                        void * t133 = GM_LD(void *, (((uint8_t *)&F.l12) + 24));
                        void * t134 = t133;
                        void * t135;
                        if ((GM_LD(void *, ((uint8_t *)&t134)) != 0)) {
                            void * t136 = GM_LD(void *, (((uint8_t *)&t134) + 0));
                            t135 = t136;
                        } else {
                            {
                                void * t138 = gm_l0;
                                gm_tl874t2atnlh3 t139 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                gm_b16_8 t140;
                                GM_ST(void *, (uint8_t *)&t140, (void *)(gm_spfda095eoqse));
                                GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(33));
                                gm_b0_1 t141 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t138, t139, t140);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            goto lc12;
                        }
                        gm_l19 = t135;
                    }
                    {
                        void * t142 = gm_l19;
                        uint8_t t143 = gm_f_is_effects_kind_uutbf9u(t142);
                        gm_l20 = t143;
                    }
                    {
                        gm_l21 = ((uint8_t)0);
                    }
                    {
                        void * t145 = gm_l19;
                        uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t145) + 8) + 0));
                        if (t146 != ((uint8_t)6ULL)) goto ie144;
                        gm_t08olq97rscn3 t147 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t145) + 8) + 8));
                        gm_l22 = t147;
                        {
                            uint8_t t149 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l22) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l22) + 0)), gm_s2737i1iq57nf, 4) == 0));
                            gm_l21 = t149;
                        }
                        goto ix144;
ie144:;
ix144:;
                    }
                    {
                        uint8_t t151 = 0;
                        uint8_t t152 = gm_l20;
                        if (!t152) {
                            uint8_t t153 = gm_l21;
                            t151 = t153;
                        } else t151 = 1;
                        if (!t151) goto ie150;
                        {
                            uint8_t t156 = gm_l3;
                            uint8_t t157 = (uint8_t)!t156;
                            if (!t157) goto ie155;
                            {
                                void * t159 = gm_l0;
                                void * t160 = gm_l19;
                                gm_tl874t2atnlh3 t161 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t160) + 72));
                                gm_b16_8 t162;
                                GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sa7gjgqof9s56));
                                GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(43));
                                gm_b0_1 t163 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t159, t161, t162);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            {
                                goto lc12;
                            }
                            goto ix155;
ie155:;
ix155:;
                        }
                        {
                            {
                                gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                gm_l23 = t165;
                            }
                            {
                                gm_l24 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t167 = gm_l24;
                                    uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    uint8_t t169 = (t167 < t168);
                                    if (!t169) goto lb166;
                                    {
                                        uint64_t t172 = gm_l24;
                                        uint64_t t173 = (uintptr_t)t172;
                                        uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                        gm_t08olq97rscn3 t177 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t173 * 32));
                                        gm_l25 = t177;
                                        {
                                            void * t179 = gm_l0;
                                            gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 0));
                                            gm_tnnjeerolos0a t181;
                                            uint8_t t183 = gm_l20;
                                            if (!t183) goto ie182;
                                            gm_tnnjeerolos0a t185;
                                            GM_ST(uint8_t, (uint8_t *)&t185 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t185 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t185 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)1ULL));
                                            gm_tnnjeerolos0a t186 = t185;
                                            t181 = t186;
                                            goto ix182;
ie182:;
                                            gm_tnnjeerolos0a t188;
                                            GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)0ULL));
                                            gm_tnnjeerolos0a t189 = t188;
                                            t181 = t189;
ix182:;
                                            uint32_t t190 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t179, t180, t181);
                                            if (gm_unw.kind) {
                                                goto gm_ll170;
                                            }
                                            gm_l26 = t190;
                                        }
                                        {
                                            void * t191 = (void *)((uint8_t *)&F.l7);
                                            uint32_t t192 = gm_l26;
                                            gm_tftb1odc739t5 t193 = gm_f_push_im9d4ro(t191, t192);
                                            gm_tftb1odc739t5 t194 = t193;
                                            if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 1, gm_srcmhupam4se3h5, 78, 17);
                                            gm_b0_1 t195 = GM_LD(gm_b0_1, (((uint8_t *)&t194) + 1));
                                        }
                                        {
                                            void * t196 = gm_l0;
                                            void * t197 = (void *)(((uint8_t *)t196) + 1488);
                                            gm_tep2drsl9408b t198;
                                            memset((uint8_t *)&t198 + 20, 0, 4);
                                            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t198 + 0, t199);
                                            void * t200 = gm_l0;
                                            void * t201 = (void *)(((uint8_t *)t200) + 8);
                                            uint32_t t202 = gm_l26;
                                            gm_t88rcucg1b7uk t203 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t201, t202);
                                            if (gm_unw.kind) {
                                                goto gm_ll170;
                                            }
                                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t198 + 16, t203);
                                            gm_tftb1odc739t5 t204 = gm_f_push_hv483nm(t197, &t198);
                                            gm_tftb1odc739t5 t205 = t204;
                                            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 1, gm_srcmhupam4se3h5, 79, 17);
                                            gm_b0_1 t206 = GM_LD(gm_b0_1, (((uint8_t *)&t205) + 1));
                                        }
                                        {
                                            void * t207 = (void *)((uint8_t *)&F.l6);
                                            gm_t5v0c8ia6a5mo t208;
                                            memset((uint8_t *)&t208 + 49, 0, 7);
                                            gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t208 + 0, t209);
                                            void * t210 = gm_l0;
                                            void * t211 = (void *)(((uint8_t *)t210) + 8);
                                            gm_t88rcucg1b7uk t212 = gm_f_type_ty_9ph2gbr((void *)NULL, cx, NULL, t211);
                                            if (gm_unw.kind) {
                                                goto gm_ll170;
                                            }
                                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t208 + 16, t212);
                                            gm_tl874t2atnlh3 t213 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l25) + 16));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t208 + 20, t213);
                                            GM_ST(void *, (uint8_t *)&t208 + 32, ((void *)0));
                                            uint32_t t214 = gm_l26;
                                            gm_t9805qtonbsag t215;
                                            GM_ST(uint8_t, (uint8_t *)&t215 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t215 + 6, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t215), t214);
                                            ((uint8_t *)&t215)[4] = 1;
                                            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t208 + 40, t215);
                                            GM_ST(uint8_t, (uint8_t *)&t208 + 48, ((uint8_t)0));
                                            gm_tftb1odc739t5 t216 = gm_f_push_o8u01sh(t207, &t208);
                                            gm_tftb1odc739t5 t217 = t216;
                                            if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_srcmhupam4se3h5, 80, 17);
                                            gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
                                        }
                                        goto me175;
mn176:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 76, 13);
me175:;
                                    }
lc166:;
                                    {
                                        uint64_t t220 = gm_l24;
                                        uint64_t r221; bool o222 = __builtin_add_overflow(t220, ((uint64_t)1ULL), &r221);
                                        uint64_t t223 = r221;
                                        gm_l24 = t223;
                                    }
                                }
gm_ll170:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb166; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc166; }
                                }
                                goto gm_ll16;
lb166:;
                            }
                        }
                        {
                            goto lc12;
                        }
                        goto ix150;
ie150:;
ix150:;
                    }
                    {
                        void * t224 = gm_l0;
                        uint64_t t225 = gm_l1;
                        void * t226 = gm_l19;
                        gm_t88rcucg1b7uk t227 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t224, t225, t226);
                        if (gm_unw.kind) {
                            goto gm_ll16;
                        }
                        gm_l27 = t227;
                    }
                    {
                        void * t229 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                        void * t230 = t229;
                        uint8_t t231;
                        if ((GM_LD(void *, ((uint8_t *)&t230)) != 0)) goto mn233;
                        t231 = ((uint8_t)0);
                        goto me232;
mn233:;
                        t231 = ((uint8_t)1);
                        goto me232;
mn234:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 85, 12);
me232:;
                        if (!t231) goto ie228;
                        {
                            gm_l8 = ((uint8_t)1);
                        }
                        goto ix228;
ie228:;
                        uint8_t t237 = gm_l8;
                        if (!t237) goto ie236;
                        uint8_t t238 = gm_l3;
                        if (!t238) goto ie236;
                        {
                            void * t240 = gm_l0;
                            gm_tl874t2atnlh3 t241 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                            gm_b16_8 t242;
                            GM_ST(void *, (uint8_t *)&t242, (void *)(gm_s0sivi9n1gpid));
                            GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(73));
                            gm_b0_1 t243 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t240, t241, t242);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                        }
                        goto ix236;
ie236:;
ix236:;
ix228:;
                    }
                    {
                        uint64_t t245 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                        uint8_t t246 = ((t245 == ((uint64_t)0ULL)));
                        if (!t246) goto ie244;
                        {
                            void * t248 = (void *)((uint8_t *)&F.l6);
                            gm_t5v0c8ia6a5mo t249;
                            memset((uint8_t *)&t249 + 49, 0, 7);
                            gm_b16_8 t250;
                            GM_ST(void *, (uint8_t *)&t250, (void *)(gm_sfdbfrv46lihd));
                            GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(1));
                            GM_ST(gm_b16_8, (uint8_t *)&t249 + 0, t250);
                            gm_t88rcucg1b7uk t251 = gm_l27;
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t249 + 16, t251);
                            gm_tl874t2atnlh3 t252 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t249 + 20, t252);
                            GM_ST(void *, (uint8_t *)&t249 + 32, ((void *)0));
                            gm_t9805qtonbsag t253;
                            memset(&t253, 0, sizeof t253);
                            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t249 + 40, t253);
                            GM_ST(uint8_t, (uint8_t *)&t249 + 48, ((uint8_t)0));
                            gm_tftb1odc739t5 t254 = gm_f_push_o8u01sh(t248, &t249);
                            gm_tftb1odc739t5 t255 = t254;
                            if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 1, gm_srcmhupam4se3h5, 91, 13);
                            gm_b0_1 t256 = GM_LD(gm_b0_1, (((uint8_t *)&t255) + 1));
                        }
                        goto ix244;
ie244:;
ix244:;
                    }
                    {
                        gm_l28 = ((uint8_t)0);
                    }
                    {
                        uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 17));
                        if (!t258) goto ie257;
                        {
                            void * t260 = gm_l0;
                            void * t261 = (void *)(((uint8_t *)t260) + 8);
                            gm_t88rcucg1b7uk t262 = gm_l27;
                            gm_t53at80pfdus3 t263;
                            gm_f_kind_jcnt4hm((void *)&t263, t261, GM_CONV(gm_csguankko4lmhd, t262));
                            gm_t53at80pfdus3 t264 = t263;
                            uint8_t t265;
                            uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&t264) + 0));
                            switch (t271) { case ((uint8_t)0ULL): goto ma269; case ((uint8_t)1ULL): goto ma269; case ((uint8_t)2ULL): goto ma269; case ((uint8_t)3ULL): goto ma269; case ((uint8_t)4ULL): goto ma269; case ((uint8_t)5ULL): goto ma269; case ((uint8_t)6ULL): goto ma269; case ((uint8_t)7ULL): goto ma269; case ((uint8_t)8ULL): goto ma269; case ((uint8_t)9ULL): goto ma269; case ((uint8_t)10ULL): goto ma269; case ((uint8_t)11ULL): goto ma269; case ((uint8_t)12ULL): goto ma267; case ((uint8_t)13ULL): goto ma269; case ((uint8_t)14ULL): goto ma269; case ((uint8_t)15ULL): goto ma269; case ((uint8_t)16ULL): goto ma269; case ((uint8_t)17ULL): goto ma269; case ((uint8_t)18ULL): goto ma269; case ((uint8_t)19ULL): goto ma268; case ((uint8_t)20ULL): goto ma269; case ((uint8_t)21ULL): goto ma269; case ((uint8_t)22ULL): goto ma269; case ((uint8_t)23ULL): goto ma269; case ((uint8_t)24ULL): goto ma269; case ((uint8_t)25ULL): goto ma269; case ((uint8_t)26ULL): goto ma268; default: goto ma269; }
ma267:;
                            uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&t264) + 0));
                            if (t273 != ((uint8_t)12ULL)) goto mn272;
                            gm_b16_8 t274 = GM_LD(gm_b16_8, ((((uint8_t *)&t264) + 8) + 16));
                            gm_l29 = t274;
                            uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            uint8_t t276 = ((t275 == ((uint64_t)0ULL)));
                            t265 = t276;
                            goto me266;
mn272:;
ma268:;
                            uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)&t264) + 0));
                            if (t280 != ((uint8_t)19ULL)) goto pa279;
                            goto po278;
pa279:;
                            uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&t264) + 0));
                            if (t282 != ((uint8_t)26ULL)) goto pa281;
                            goto po278;
pa281:;
                            goto mn277;
po278:;
                            t265 = ((uint8_t)1);
                            goto me266;
mn277:;
ma269:;
                            t265 = ((uint8_t)0);
                            goto me266;
mn283:;
ma270:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 96, 22);
me266:;
                            gm_l30 = t265;
                        }
                        {
                            uint8_t t285 = gm_l3;
                            uint8_t t286 = (uint8_t)!t285;
                            if (!t286) goto ie284;
                            {
                                void * t288 = gm_l0;
                                gm_tl874t2atnlh3 t289 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                gm_b16_8 t290;
                                GM_ST(void *, (uint8_t *)&t290, (void *)(gm_sfvsgjcg86bbl));
                                GM_ST(uintptr_t, (uint8_t *)&t290 + 8, (uintptr_t)(44));
                                gm_b0_1 t291 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t288, t289, t290);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            goto ix284;
ie284:;
                            uint8_t t293 = 0;
                            uint8_t t294 = 0;
                            uint64_t t295 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                            uint8_t t296 = (!(t295 == ((uint64_t)1ULL)));
                            if (!t296) {
                                void * t297 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                                void * t298 = t297;
                                uint8_t t299;
                                if ((GM_LD(void *, ((uint8_t *)&t298)) != 0)) goto mn301;
                                t299 = ((uint8_t)0);
                                goto me300;
mn301:;
                                t299 = ((uint8_t)1);
                                goto me300;
mn302:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 104, 47);
me300:;
                                t294 = t299;
                            } else t294 = 1;
                            if (!t294) {
                                uint8_t t303 = gm_l30;
                                uint8_t t304 = (uint8_t)!t303;
                                t293 = t304;
                            } else t293 = 1;
                            if (!t293) goto ie292;
                            {
                                void * t306 = gm_l0;
                                gm_tl874t2atnlh3 t307 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                gm_b16_8 t308;
                                GM_ST(void *, (uint8_t *)&t308, (void *)(gm_s0ad3307s8jmo));
                                GM_ST(uintptr_t, (uint8_t *)&t308 + 8, (uintptr_t)(104));
                                gm_b0_1 t309 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t306, t307, t308);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            goto ix292;
ie292:;
                            {
                                gm_l28 = ((uint8_t)1);
                            }
ix292:;
ix284:;
                        }
                        goto ix257;
ie257:;
ix257:;
                    }
                    {
                        {
                            gm_b16_8 t312 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            gm_l31 = t312;
                        }
                        {
                            gm_l32 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t314 = gm_l32;
                                uint64_t t315 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                                uint8_t t316 = (t314 < t315);
                                if (!t316) goto lb313;
                                {
                                    uint64_t t319 = gm_l32;
                                    uint64_t t320 = (uintptr_t)t319;
                                    uint64_t t321 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                                    gm_t08olq97rscn3 t324 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t320 * 32));
                                    gm_l33 = t324;
                                    {
                                        void * t326 = (void *)((uint8_t *)&F.l6);
                                        gm_t5v0c8ia6a5mo t327;
                                        memset((uint8_t *)&t327 + 49, 0, 7);
                                        gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t327 + 0, t328);
                                        gm_t88rcucg1b7uk t329 = gm_l27;
                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t327 + 16, t329);
                                        gm_tl874t2atnlh3 t330 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l33) + 16));
                                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t327 + 20, t330);
                                        void * t331 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                                        GM_ST(void *, (uint8_t *)&t327 + 32, t331);
                                        gm_t9805qtonbsag t332;
                                        memset(&t332, 0, sizeof t332);
                                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t327 + 40, t332);
                                        uint8_t t333 = gm_l28;
                                        GM_ST(uint8_t, (uint8_t *)&t327 + 48, t333);
                                        gm_tftb1odc739t5 t334 = gm_f_push_o8u01sh(t326, &t327);
                                        gm_tftb1odc739t5 t335 = t334;
                                        if (((((uint8_t *)&t335))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t335) + 1, gm_srcmhupam4se3h5, 112, 13);
                                        gm_b0_1 t336 = GM_LD(gm_b0_1, (((uint8_t *)&t335) + 1));
                                    }
                                    goto me322;
mn323:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 111, 9);
me322:;
                                }
lc313:;
                                {
                                    uint64_t t338 = gm_l32;
                                    uint64_t r339; bool o340 = __builtin_add_overflow(t338, ((uint64_t)1ULL), &r339);
                                    uint64_t t341 = r339;
                                    gm_l32 = t341;
                                }
                            }
lb313:;
                        }
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 33, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t343 = gm_l11;
                    uint64_t r344; bool o345 = __builtin_add_overflow(t343, ((uint64_t)1ULL), &r344);
                    uint64_t t346 = r344;
                    gm_l11 = t346;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        {
            gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t349 = t348;
            gm_b16_8 t350 = GM_LD(gm_b16_8, ((uint8_t *)&t349));
            gm_l34 = t350;
        }
        {
            gm_l35 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t352 = gm_l35;
                uint64_t t353 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                uint8_t t354 = (t352 < t353);
                if (!t354) goto lb351;
                {
                    gm_tgsvmddj3e85e t357;
                    uint64_t t358 = gm_l35;
                    GM_ST(uint64_t, (uint8_t *)&t357 + 0, t358);
                    uint64_t t359 = gm_l35;
                    uint64_t t360 = (uintptr_t)t359;
                    uint64_t t361 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                    memcpy((uint8_t *)&t357 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t360 * 56), 56);
                    gm_tgsvmddj3e85e t362 = t357;
                    uint64_t t365 = GM_LD(uint64_t, (((uint8_t *)&t362) + 0));
                    gm_l36 = t365;
                    gm_t5v0c8ia6a5mo t366 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t362) + 8));
                    F.l37 = t366;
                    {
                        uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)&F.l37) + 48));
                        uint8_t t370 = (uint8_t)!t369;
                        if (!t370) goto ie368;
                        {
                            goto lc351;
                        }
                        goto ix368;
ie368:;
ix368:;
                    }
                    {
                        {
                            uint64_t t373 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                            void * t374 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)));
                            uint64_t t375 = gm_l36;
                            uint64_t r376; bool o377 = __builtin_add_overflow(t375, ((uint64_t)1ULL), &r376);
                            if (o377) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 118, 31);
                            uint64_t t378 = r376;
                            uint64_t t379 = (uintptr_t)t378;
                            if (t379 > (uintptr_t)t373 || (uintptr_t)t373 > (uintptr_t)t373) gm_trap_slice(t379, (uintptr_t)t373, t373, gm_srcmhupam4se3h5, 118, 18);
                            gm_b16_8 t380;
                            GM_ST(void *, (uint8_t *)&t380, (void *)(((uint8_t *)t374 + t379 * 56)));
                            GM_ST(uintptr_t, (uint8_t *)&t380 + 8, (uintptr_t)((uintptr_t)t373 - t379));
                            gm_l38 = t380;
                        }
                        {
                            gm_l39 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t382 = gm_l39;
                                uint64_t t383 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                uint8_t t384 = (t382 < t383);
                                if (!t384) goto lb381;
                                {
                                    uint64_t t387 = gm_l39;
                                    uint64_t t388 = (uintptr_t)t387;
                                    uint64_t t389 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                    gm_t5v0c8ia6a5mo t392 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t388 * 56));
                                    F.l40 = t392;
                                    {
                                        uint8_t t395 = 0;
                                        uint8_t t396 = GM_LD(uint8_t, (((uint8_t *)&F.l40) + 48));
                                        if (!t396) {
                                            void * t397 = (void *)((uint8_t *)&F.l40);
                                            uint8_t t398 = gm_f_is_site_8sr7vam(t397);
                                            uint8_t t399 = (uint8_t)!t398;
                                            t395 = t399;
                                        } else t395 = 1;
                                        if (!t395) goto ie394;
                                        {
                                            void * t401 = gm_l0;
                                            gm_tl874t2atnlh3 t402 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 20));
                                            gm_b16_8 t403;
                                            GM_ST(void *, (uint8_t *)&t403, (void *)(gm_svurqcoqldl32));
                                            GM_ST(uintptr_t, (uint8_t *)&t403 + 8, (uintptr_t)(82));
                                            gm_b0_1 t404 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t401, t402, t403);
                                            if (gm_unw.kind) {
                                                goto gm_ll385;
                                            }
                                        }
                                        {
                                            gm_l5 = ((uint8_t)0);
                                        }
                                        {
                                            goto lb381;
                                        }
                                        goto ix394;
ie394:;
ix394:;
                                    }
                                    goto me390;
mn391:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 118, 9);
me390:;
                                }
lc381:;
                                {
                                    uint64_t t406 = gm_l39;
                                    uint64_t r407; bool o408 = __builtin_add_overflow(t406, ((uint64_t)1ULL), &r407);
                                    uint64_t t409 = r407;
                                    gm_l39 = t409;
                                }
                            }
gm_ll385:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb381; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc381; }
                            }
                            goto gm_ll355;
lb381:;
                        }
                    }
                    {
                        goto lb351;
                    }
                    goto me363;
mn364:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 116, 5);
me363:;
                }
lc351:;
                {
                    uint64_t t411 = gm_l35;
                    uint64_t r412; bool o413 = __builtin_add_overflow(t411, ((uint64_t)1ULL), &r412);
                    uint64_t t414 = r412;
                    gm_l35 = t414;
                }
            }
gm_ll355:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb351; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc351; }
            }
            goto gm_lad;
lb351:;
        }
    }
    {
        void * t416 = gm_l2;
        uint8_t t417 = GM_LD(uint8_t, (((uint8_t *)t416) + 16));
        if (!t417) goto ie415;
        void * t418 = gm_l0;
        uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)t418) + 1364));
        uint8_t t420 = (uint8_t)!t419;
        if (!t420) goto ie415;
        {
            void * t422 = gm_l0;
            void * t423 = gm_l2;
            gm_tl874t2atnlh3 t424 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t423) + 56));
            gm_b16_8 t425;
            GM_ST(void *, (uint8_t *)&t425, (void *)(gm_sba4cjt08ps1i));
            GM_ST(uintptr_t, (uint8_t *)&t425 + 8, (uintptr_t)(57));
            gm_b0_1 t426 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t422, t424, t425);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l5 = ((uint8_t)0);
        }
        goto ix415;
ie415:;
ix415:;
    }
    {
        void * t427 = gm_l0;
        void * t428 = (void *)(((uint8_t *)t427) + 8);
        gm_t88rcucg1b7uk t429 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t428);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l41 = t429;
    }
    {
        void * t431 = gm_l2;
        void * t432 = GM_LD(void *, (((uint8_t *)t431) + 24));
        void * t433 = t432;
        if (!(GM_LD(void *, ((uint8_t *)&t433)) != 0)) goto ie430;
        void * t434 = GM_LD(void *, ((uint8_t *)&t433));
        gm_l42 = t434;
        {
            void * t436 = gm_l0;
            uint64_t t437 = gm_l1;
            void * t438 = gm_l42;
            gm_t88rcucg1b7uk t439 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t436, t437, t438);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l41 = t439;
        }
        {
            void * t441 = gm_l0;
            void * t442 = (void *)(((uint8_t *)t441) + 8);
            gm_t88rcucg1b7uk t443 = gm_l41;
            gm_t53at80pfdus3 t444;
            gm_f_kind_jcnt4hm((void *)&t444, t442, GM_CONV(gm_csguankko4lmhd, t443));
            gm_t53at80pfdus3 t445 = t444;
            uint8_t t446;
            uint8_t t449 = GM_LD(uint8_t, (((uint8_t *)&t445) + 0));
            if (t449 != ((uint8_t)15ULL)) goto mn448;
            t446 = ((uint8_t)1);
            goto me447;
mn448:;
            t446 = ((uint8_t)0);
            goto me447;
mn450:;
            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 134, 12);
me447:;
            if (!t446) goto ie440;
            {
                void * t452 = gm_l0;
                void * t453 = gm_l42;
                gm_tl874t2atnlh3 t454 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t453) + 72));
                gm_b16_8 t455;
                GM_ST(void *, (uint8_t *)&t455, (void *)(gm_sdsd71bjtv81c));
                GM_ST(uintptr_t, (uint8_t *)&t455 + 8, (uintptr_t)(97));
                gm_b0_1 t456 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t452, t454, t455);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix440;
ie440:;
ix440:;
        }
        goto ix430;
ie430:;
ix430:;
    }
    {
        gm_tnpifv20irnph t457 = gm_f_empty_effects_r0iv3pv();
        gm_l43 = t457;
    }
    {
        void * t459 = gm_l2;
        if (!(((((uint8_t *)t459) + 32))[16] != 0)) goto ie458;
        gm_b16_8 t460 = GM_LD(gm_b16_8, (((uint8_t *)t459) + 32));
        gm_l44 = t460;
        {
            void * t462 = gm_l0;
            uint64_t t463 = gm_l1;
            gm_b16_8 t464 = gm_l44;
            gm_b16_8 t465 = t464;
            gm_b16_8 t466 = GM_LD(gm_b16_8, ((uint8_t *)&t465));
            gm_tnpifv20irnph t467 = gm_f_resolve_effect_list_uhjlh6o((void *)NULL, cx, NULL, t462, t463, t466);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t467;
        }
        goto ix458;
ie458:;
ix458:;
    }
    {
        {
            gm_b16_8 t469 = gm_l4;
            gm_l45 = t469;
        }
        {
            gm_l46 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t471 = gm_l46;
                uint64_t t472 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                uint8_t t473 = (t471 < t472);
                if (!t473) goto lb470;
                {
                    uint64_t t476 = gm_l46;
                    uint64_t t477 = (uintptr_t)t476;
                    uint64_t t478 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                    gm_tpv68m88f9lcr t481 = GM_LD(gm_tpv68m88f9lcr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l45)) + t477 * 56));
                    F.l47 = t481;
                    {
                        void * t483 = gm_l0;
                        uint64_t t484 = gm_l1;
                        void * t485 = (void *)((uint8_t *)&F.l47);
                        gm_b0_1 t486 = gm_f_declare_constraint_s1juggf((void *)NULL, cx, NULL, t483, t484, t485);
                        if (gm_unw.kind) {
                            goto gm_ll474;
                        }
                    }
                    goto me479;
mn480:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 140, 5);
me479:;
                }
lc470:;
                {
                    uint64_t t488 = gm_l46;
                    uint64_t r489; bool o490 = __builtin_add_overflow(t488, ((uint64_t)1ULL), &r489);
                    uint64_t t491 = r489;
                    gm_l46 = t491;
                }
            }
gm_ll474:;
            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb470; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc470; }
            }
            goto gm_lad;
lb470:;
        }
    }
    {
        void * t492 = gm_l0;
        gm_b0_1 t493 = gm_f_end_constraint_defer_52jt3m9((void *)NULL, cx, NULL, t492);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t495 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
        uint8_t t496 = (t495 > ((uint64_t)0ULL));
        if (!t496) goto ie494;
        {
            void * t498 = gm_l0;
            void * t499 = gm_l2;
            gm_tl874t2atnlh3 t500 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t499) + 56));
            gm_b16_8 t501 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 0));
            gm_b0_1 t502 = gm_f_check_distinct_d4svq2h((void *)NULL, cx, NULL, t498, t500, t501);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix494;
ie494:;
ix494:;
    }
    {
        void * t503 = gm_l0;
        gm_f_restore_scope_edqq1v8(t503, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l9)));
    }
    {
        uint8_t t505 = gm_l5;
        uint8_t t506 = (uint8_t)!t505;
        if (!t506) goto ie504;
        {
            gm_tg93b272d11vo t508;
            memset(&t508, 0, sizeof t508);
            uint8_t t509 = 0;
            F.ret = t508;
            goto gm_fret;
        }
        goto ix504;
ie504:;
ix504:;
    }
    {
        gm_tqqhoivahirr0 t510;
        memset((uint8_t *)&t510 + 28, 0, 4);
        memset((uint8_t *)&t510 + 60, 0, 4);
        memset((uint8_t *)&t510 + 129, 0, 7);
        gm_b16_8 t511;
        GM_ST(void *, (uint8_t *)&t511, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t511 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&t510 + 0, t511);
        void * t512 = gm_l2;
        gm_tl874t2atnlh3 t513 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t512) + 56));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t510 + 16, t513);
        uint64_t t514 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t510 + 32, t514);
        gm_b16_8 t515 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t510 + 40, t515);
        gm_t88rcucg1b7uk t516 = gm_l41;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t510 + 56, t516);
        gm_tnpifv20irnph t517 = gm_l43;
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&t510 + 64, t517);
        gm_b16_8 t518 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t510 + 80, t518);
        gm_tqu3m2l3n8mnc t519;
        memset(&t519, 0, sizeof t519);
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t510 + 96, t519);
        GM_ST(uint8_t, (uint8_t *)&t510 + 128, ((uint8_t)0));
        gm_tg93b272d11vo t520;
        GM_ST(uint8_t, (uint8_t *)&t520 + 137, 0);
        GM_ST(uint16_t, (uint8_t *)&t520 + 138, 0);
        GM_ST(uint32_t, (uint8_t *)&t520 + 140, 0);
        GM_ST(gm_tqqhoivahirr0, ((uint8_t *)&t520), t510);
        ((uint8_t *)&t520)[136] = 1;
        uint8_t t521 = 0;
        F.ret = t520;
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
