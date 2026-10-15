# Module `std/net`

[All modules](index.md)

Helpers over the `Net` effect (§21.7) for TCP connections.

    net :: import "std/net"
    s := try connect("localhost", 8080)
    defer close_socket(s)
    try net.send_all(s, "ping\n")
    reply: [5]u8 = ---
    try net.recv_exact(s, reply[..])

## Functions

### `send_all`

```gamma
send_all :: fn(s: Socket, bytes: []const u8) -> IoError!() \ {Net}
```

Sends all of `bytes` on `s`, retrying short sends. A send that makes no progress (returns
0) fails with `.Other(0)`.

### `recv_exact`

```gamma
recv_exact :: fn(s: Socket, buf: []u8) -> IoError!() \ {Net}
```

Fills `buf` completely from `s`. `UnexpectedEof` if the peer closes the connection
first.
