# Module `std/net`

[All modules](index.md)

Helpers over the `Net` effect (§21.7) for TCP connections.

    net :: import "std/net"
    s := try Net.connect("localhost", 8080)
    defer Net.close(s)
    try net.send_all(s, "ping\n")
    reply: [5]u8 = ---
    try net.recv_exact(s, reply[..])

The module also has IP address values (`Ipv4`, `Ipv6`, `IpAddr`) with strict text
parsing and RFC 5952 formatting into a caller-supplied buffer. They are plain values
and need no effect. The text `format_ip` writes is a valid host for `Net.connect` and
`Net.listen` (numeric hosts are passed to the system resolver as they are, without
brackets):

    addr := try net.parse_ip("2001:db8::1")
    buf: [net.IPV6_LEN]u8 = ---
    s := try Net.connect(net.format_ip(addr, buf[..]), 8080)

## Structs

### `Ipv4`

```gamma
Ipv4 :: struct {
    pub octets: [4]u8
}
```

An IPv4 address.

Fields:

- `octets`: The four bytes in network order: `192.0.2.1` is `192, 0, 2, 1`.

### `Ipv6`

```gamma
Ipv6 :: struct {
    pub octets: [16]u8
}
```

An IPv6 address.

Fields:

- `octets`: The sixteen bytes in network order.

## Unions

### `IpAddr`

```gamma
IpAddr :: union {
    V4: Ipv4
    V6: Ipv6
}
```

An address of either family.

### `ParseError`

```gamma
ParseError :: union {
    Syntax: usize
    Range: usize
}
```

Why parsing an address failed. Each variant carries a byte index into the text. The
problem nearest the start is reported, except that a field out of range is reported
as soon as its digits have been read.

Variants:

- `Syntax`: The text does not have the expected shape: the index of the first byte that does
  not fit, or the length of the text if it ended too early. Also used for an octet
  with a leading zero (the index of the digit after the zero), for a second `::`
  (the index of its second colon), and for too many groups or a `::` that stands for
  no group (the index of the group that does not fit, or of the `::`).
- `Range`: The shape is right but a number is too big: an IPv4 octet above 255 (including one
  with more than three digits), or an IPv6 group with more than four hex digits. The
  index is where that field starts.

## Type aliases

### `Socket`

```gamma
Socket :: core.Socket
```

A TCP connection (§21.7).

### `Listener`

```gamma
Listener :: core.Listener
```

A TCP socket listening for connections.

### `Shutdown`

```gamma
Shutdown :: core.Shutdown
```

Which direction of a connection `Net.shutdown` closes.

## Constants

### `IPV4_LEN`

```gamma
IPV4_LEN :: 15
```

Maximum length of an IPv4 address in text: `255.255.255.255`.

### `IPV6_LEN`

```gamma
IPV6_LEN :: 45
```

A length that always suffices for an IPv6 address in text. The longest form
`format_ipv6` writes is 39 bytes (`ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff`); 45 is the
longest form with a dotted IPv4 tail, which `parse_ipv6` accepts.

## Functions

### `parse_ipv4`

```gamma
parse_ipv4 :: fn(s: string) -> ParseError!Ipv4
```

Parses an IPv4 address in dotted decimal: exactly four decimal fields of 0 to 255
separated by `.`, with nothing else around them.

The form is strict: a field with a leading zero (`01`, which some systems read as
octal) is `Syntax`, as are whitespace, a missing or extra field and the shorthand forms
(`127.1`, `0x7f.0.0.1`). A field above 255 is `Range` at the start of that field.

### `parse_ipv6`

```gamma
parse_ipv6 :: fn(s: string) -> ParseError!Ipv6
```

Parses an IPv6 address in the text forms of RFC 4291 section 2.2.

That is eight groups of 1 to 4 hexadecimal digits (either case) separated by `:`;
`::`, at most once, standing for one or more groups of zeros; and, as the last field, an
embedded dotted IPv4 address in place of the last two groups (`::ffff:1.2.3.4`,
`64:ff9b::192.0.2.33`), which follows the rules of `parse_ipv4`. Brackets (`[::1]`) and
zone identifiers (`fe80::1%eth0`) are not part of an address and are `Syntax` errors.
A group with more than four digits is `Range` at its start.

### `parse_ip`

```gamma
parse_ip :: fn(s: string) -> ParseError!IpAddr
```

Parses an address of either family: `parse_ipv6` if `s` contains a `:`, `parse_ipv4`
otherwise.

### `format_ipv4`

```gamma
format_ipv4 :: fn(a: Ipv4, buf: []u8) -> string
```

Formats `a` as dotted decimal (`192.0.2.1`) into `buf` and returns the part written.
`buf` must hold the text, at most `IPV4_LEN` bytes (a trap otherwise).

### `format_ipv6`

```gamma
format_ipv6 :: fn(a: Ipv6, buf: []u8) -> string
```

Formats `a` in the canonical form of RFC 5952 into `buf` and returns the part written.

Digits are lowercase without leading zeros. The longest run of two or more zero groups
is written `::` (the first one when runs are equally long); a single zero group is
written `0`. An IPv4-mapped address (`::ffff:0:0/96`) is written with a dotted IPv4
tail, `::ffff:192.0.2.1`, as section 5 of the RFC recommends; every other address is
all hex. `buf` must hold the text, at most `IPV6_LEN` bytes (a trap otherwise).

### `format_ip`

```gamma
format_ip :: fn(a: IpAddr, buf: []u8) -> string
```

Formats `a` with `format_ipv4` or `format_ipv6`. A buffer of `IPV6_LEN` bytes always
suffices.

### `is_loopback`

```gamma
is_loopback :: fn(a: IpAddr) -> bool
```

Whether `a` is a loopback address: `127.0.0.0/8` or `::1`. An IPv4-mapped IPv6 address
is judged as the IPv6 address it is, so `::ffff:127.0.0.1` is not loopback here.

### `is_unspecified`

```gamma
is_unspecified :: fn(a: IpAddr) -> bool
```

Whether `a` is the unspecified address: `0.0.0.0` or `::`.

### `send_all`

```gamma
send_all :: fn(s: Socket, bytes: []const u8) -> IoError!() needs Net
```

Sends all of `bytes` on `s`, retrying short sends. A send that makes no progress (returns
0) fails with `.Other(0)`.

### `recv_exact`

```gamma
recv_exact :: fn(s: Socket, buf: []u8) -> IoError!() needs Net
```

Fills `buf` completely from `s`. `UnexpectedEof` if the peer closes the connection
first.
