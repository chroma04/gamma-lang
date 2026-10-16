# std modules

| Module | Description |
| --- | --- |
| [`std/arena`](arena.md) | An allocator that takes memory from a parent allocator in growing chunks and gives it all back at once (§15.2). |
| [`std/ascii`](ascii.md) | Classification and case of single bytes. |
| [`std/bytes`](bytes.md) | Reading and writing binary data. |
| [`std/c`](c.md) | C's platform-dependent types (§19.1), for declaring foreign functions. |
| [`std/cli`](cli.md) | Command-line parsing as an iterator, getopt style. |
| [`std/collections/bitset`](collections/bitset.md) | A set of small unsigned integers, one bit each. |
| [`std/collections/deque`](collections/deque.md) | A double-ended queue in a growable ring buffer. |
| [`std/collections/heap`](collections/heap.md) | A binary heap, used as a priority queue. |
| [`std/collections/ordered_map`](collections/ordered_map.md) | A map that keeps its keys in order, as a B-tree. |
| [`std/encoding/base64`](encoding/base64.md) | Base64 as in RFC 4648, with the standard alphabet (section 4) or the URL and filename safe one (section 5), with or without `=` padding. |
| [`std/encoding/hex`](encoding/hex.md) | Base16 as in RFC 4648 section 8, two hexadecimal digits per byte. |
| [`std/fiber`](fiber.md) | A cooperative round-robin scheduler for fibers (§14). |
| [`std/fiber/channel`](fiber/channel.md) | A bounded channel between the tasks of a `std/fiber` scheduler. |
| [`std/fiber/waiters`](fiber/waiters.md) | The queue of waiting tasks of std/fiber, which re-exports it. |
| [`std/fixed_buffer`](fixed_buffer.md) | An allocator over a buffer the caller owns (§15.2). |
| [`std/fmt`](fmt.md) | Number formatting into caller-provided buffers. |
| [`std/fs`](fs.md) | Whole-file operations over the `Fs` effect (§21.7). |
| [`std/hash`](hash.md) | Fixed, unkeyed digests and checksums for integrity checks. |
| [`std/hash/crc32`](hash/crc32.md) | The CRC-32 of ISO-HDLC (ISO 3309, ITU-T V.42), as used by zlib, gzip, PNG and Ethernet: reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF, final exclusive-or 0xFFFFFFFF. |
| [`std/hash/sha256`](hash/sha256.md) | SHA-256 as in FIPS 180-4. |
| [`std/io`](io.md) | Reading lines from `In`, and handlers that connect `In` and `Out` to files and strings, so code written against the ambient effects works on any of them. |
| [`std/json`](json.md) | JSON as in RFC 8259 (and ECMA-404): a parser into a value tree, and a writer. |
| [`std/log`](log.md) | Logging as an effect. |
| [`std/math`](math.md) | Elementary functions on `f64`, written entirely in Gamma. |
| [`std/net`](net.md) | Helpers over the `Net` effect (§21.7) for TCP connections. |
| [`std/path`](path.md) | POSIX path strings, with `/` as the separator. |
| [`std/random`](random.md) | A fast seedable generator, and helpers that draw from `Rand`. |
| [`std/str`](str.md) | Byte-oriented string utilities. |
| [`std/text`](text.md) | Unicode text segmentation and UTF-8 encoding of scalar values. |
| [`std/thread`](thread.md) | Threads and the synchronization built on futexes (§19.6). |
| [`std/thread/wait_group`](thread/wait_group.md) | The wait group of std/thread, which re-exports it. |
| [`std/time`](time.md) | Calendar dates and times, RFC 3339 text, and durations as text. |
