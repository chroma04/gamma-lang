# Module `std/net`

[All modules](index.md)

Helpers over the `Net` effect (§21.7) for TCP connections.

    net :: import "std/net"
    s := try connect("localhost", 8080)
    defer close_socket(s)
    try net.send_all(s, "ping\n")
    reply: [5]u8 = ---
    try net.recv_exact(s, reply[..])

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

## Functions

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
