# URI Syntax

## General Syntax

Transmission mediums are specified in the standard URI format:

```yaml
SCHEME://HOST:PORT?PARAM1=VALUE1&PARAM2=VALUE2&...
```

`srt-xtransmit` supports the following schemes:

- `srt` - SRT connection
- `udp` - UDP (unicast and multicast)
- `file` - file/folder

## SRT Specific Query

```yaml
srt://<host>:port?field1=value1&field2=value2&...
```

### SRT Specific Fields

There are some query fields that do not map into SRT socket options. They are handled by the application.

| Query Field          | Values                                       | Description                 |
| -------------------- | -------------------------------------------- | --------------------------- |
| `mode`               | - caller </br> - listener </br> - rendezvous | Connection mode.                    |
| `blocking`           | `true` / `false`                             | Enable/disable blocking mode.       |
| `bind`               | `<ip>:port`                                  | Bind socket to a specific NIC/port. |

### SRT Socket Options

| Query Field          | Values           | SRT Socket Option         | Description                 |
| -------------------- | ---------------- | ------------------------- | --------------------------- |
| `congestion`         | {`live`, `file`} | `SRTO_CONGESTION`         | Type of congestion control. |
| `conntimeo`          | `ms`             | `SRTO_CONNTIMEO`          | Connection timeout. |
| `drifttracer`        | `bool`           | `SRTO_DRIFTTRACER`        | Enable drift tracer. |
| `enforcedencryption` | `bool`           | `SRTO_ENFORCEDENCRYPTION` | Reject connection if parties set different passphrase. |
| `fc`                 | `bytes`          | `SRTO_FC`                 | Flow control window size. |
| `groupconnect`       | {`0`, `1`}       | `SRTO_GROUPCONNECT`       | Accept group connections. |
| `groupstabtimeo`     | `ms`             | `SRTO_GROUPSTABTIMEO`     | Group stability timeout. |
| `inputbw`            | `bytes`          | `SRTO_INPUTBW`            | Input bandwidth. |
| `iptos`              | 0..255           | `SRTO_IPTOS`              | IP socket type of service. |
| `ipttl`              | 1..255           | `SRTO_IPTTL`              | Defines IP socket "time to live" option. |
| `ipv6only`           | -1..1            | `SRTO_IPV6ONLY`           | Allow only IPv6. |
| `kmpreannounce`      | 0..              | `SRTO_KMPREANNOUNCE`      | Duration of Stream Encryption key switchover (in packets). |
| `kmrefreshrate`      | 0..              | `SRTO_KMREFRESHRATE`      | Stream encryption key refresh rate (in packets). |
| `latency`            | 0..              | `SRTO_LATENCY`            | Defines the maximum accepted transmission latency. |
| `linger`             | 0..              | `SRTO_LINGER`             | Link linger value. |
| `lossmaxttl`         | 0..              | `SRTO_LOSSMAXTTL`         | Packet reorder tolerance. |
| `maxbw`              | 0..              | `SRTO_MAXBW`              | Bandwidth limit in bytes. |
| `messageapi`         | `bool`           | `SRTO_MESSAGEAPI`         | Enable SRT message mode. |
| `minversion`         | maj.min.rev      | `SRTO_MINVERSION`         | Minimum SRT library version of a peer. |
| `mss`                | 76..             | `SRTO_MSS`                | MTU size. |
| `nakreport`          | `bool`           | `SRTO_NAKREPORT`          | Enables/disables periodic NAK reports. |
| `oheadbw`            | 5..100           | `SRTO_OHEADBW`            | limits bandwidth overhead, percents. |
| `packetfilter`       | `string`         | `SRTO_PACKETFILTER`       | Set up the packet filter. |
| `passphrase`         | `string`         | `SRTO_PASSPHRASE`         | Password for the encrypted transmission. |
| `payloadsize`        | 0..              | `SRTO_PAYLOADSIZE`        | Maximum payload size. |
| `pbkeylen`           | {16, 24, 32}     | `SRTO_PBKEYLEN`           | Crypto key length in bytes. |
| `peeridletimeo`      | `ms`             | `SRTO_PEERIDLETIMEO`      | Peer idle timeout. |
| `peerlatency`        | `ms`             | `SRTO_PEERLATENCY`        | Minimum receiver latency to be requested by sender. |
| `rcvbuf`             | `bytes`          | `SRTO_RCVBUF`             | Receiver buffer size. |
| `rcvlatency`         | `ms`             | `SRTO_RCVLATENCY`         | Receiver-side latency. |
| `retransmitalgo`     | {`0`, `1`}       | `SRTO_RETRANSMITALGO`     | Packet retransmission algorithm to use. |
| `sndbuf`             | `bytes`          | `SRTO_SNDBUF`             | Sender buffer size. |
| `snddropdelay`       | `ms`             | `SRTO_SNDDROPDELAY`       | Sender's delay before dropping packets. |
| `streamid`           | `string`         | `SRTO_STREAMID`           | Stream ID (settable in caller mode only, visible on the listener peer). |
| `tlpktdrop`          | `bool`           | `SRTO_TLPKTDROP`          | Drop too late packets. |
| `transtype`          | {`live`, `file`} | `SRTO_TRANSTYPE`          | Transmission type. |
| `tsbpdmode`          | `bool`           | `SRTO_TSBPDMODE`          | Timestamp-based packet delivery mode. |

### SRT Connection Modes

SRT can be connected using one of three connection modes:

- **caller**: the "agent" (this application) sends the connection request to
the peer **listener** in order to initiate the
connection.

- **listener**: the "agent" waits to be contacted by any peer **caller**.
Note that a listener can accept multiple callers, but *srt-live-transmit*
does not use this ability; after the first connection, it no longer
accepts new connections.

- **rendezvous**: A one-to-one only connection where both parties are
equivalent and both connect to one another simultaneously. Whichever party happened
to start first (or succeeded in passing through the firewall) is considered to have
initiated the connection.

### SRT Example URIs

- `srt://:5000` defines listener mode with IPv4.

- `srt://[::]:5000` defines caller mode (!) with IPv6.

- `srt://[::]:5000?mode=listener` defines listener mode with IPv6. If the
default value for `IPV6_V6ONLY` system socket option is 0, it will also accept IPv4 
connections.

- `srt://192.168.0.5:5000?mode=rendezvous` will make a rendezvous connection
with local address `INADDR_ANY` (IPv4) and port 5000 to a destination with
port 5000.

- `srt://[::1]:5000?mode=rendezvous&port=4000` will make a rendezvous
connection with local address `inaddr6_any` (IPv6) and port 4000 to a
destination with port 5000.

- `srt://[::1]:5000?adapter=127.0.0.1&mode=rendezvous` - this URI is invalid
(different IP versions for binding and target address)
