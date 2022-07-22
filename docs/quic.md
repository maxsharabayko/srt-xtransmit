# QUIC Socket

Current state: server does not send handshake reply.

```shell
srt-xtransmit receive "quic://:4433?tlskey=./cert.key&tlscert=./cert.crt" -v --loglevel debug
16:58:47.883332 [I] Log level set to debug
16:58:47.886441 [I] SOCKET::UDP SA requested '0.0.0.0:4433'.
16:58:47.886945 [I] SOCKET::UDP getsockname returned 16 bytes.
16:58:47.887421 [I] SOCKET::UDP udp://:4433: bound to '0.0.0.0:4433'.
16:58:47.893133 [W] CONN Listening for incoming connections.
16:58:47.893738 [I] SOCKET::UDP getsockname returned 16 bytes.
16:58:49.743561 [D] SOCKET::QUIC udp::read() received 1200 bytes.
16:58:49.743750 [I] SOCKET::QUIC version negotiation
16:58:50.746484 [D] SOCKET::QUIC udp::read() received 1200 bytes.
16:58:50.746784 [I] SOCKET::QUIC Negotiated version 1
16:58:50.747146 [I] SOCKET::QUIC stateless retry
16:58:51.756768 [D] SOCKET::QUIC udp::read() received 1200 bytes.
16:58:51.757140 [I] SOCKET::QUIC Negotiated version 1
16:58:51.757432 [I] SOCKET::UDP getsockname returned 16 bytes.
16:58:51.757816 [I] SOCKET::QUIC Accepted connection.
16:58:51.758030 [D] quiche: 799522464dcec951c5cd83188d1a2269 rx pkt Initial version=1 dcid=9562495582ed372160b02468817aaa4a scid=799522464dcec951c5cd83188d1a2269 token=7175696368650200f6217f00000100000000000000007ff87ba28b9af8533c2d3b92c74171ee len=289 pn=2
16:58:51.758207 [D] quiche: 799522464dcec951c5cd83188d1a2269 rx frm CRYPTO off=0 len=268
16:58:51.758447 [D] quiche::tls: checking peer ALPN Ok("hq-interop") against Ok("hq-interop")
16:58:51.758910 [D] quiche::tls: 799522464dcec951c5cd83188d1a2269 write message lvl=Initial len=90
16:58:51.759086 [D] quiche::tls: 799522464dcec951c5cd83188d1a2269 set write secret lvl=Handshake
16:58:51.760409 [D] quiche::tls: 799522464dcec951c5cd83188d1a2269 write message lvl=Handshake len=1189
16:58:51.760542 [D] quiche::tls: 799522464dcec951c5cd83188d1a2269 set write secret lvl=OneRTT
16:58:51.760678 [D] quiche::tls: 799522464dcec951c5cd83188d1a2269 set read secret lvl=Handshake
16:58:51.760971 [D] quiche: 799522464dcec951c5cd83188d1a2269 dropped invalid packet
```



## Build

The Quiche project profides C API and library.
The official readme section on building: [link](https://github.com/cloudflare/quiche#building).

```shell
cargo build --examples --features ffi
```

List all supported targets for RUST.
```shell
rustc --print target-list
rustc --print target-features
```


## Command-Line Syntax

Example establish a QUIC connection and just try to maintain it.

```shell
(client receiver)
srt-xtransmit receive "quic://127.0.0.1:4433" -v --loglevel debug

(server receiver)
srt-xtransmit receive "quic://:4433?tlskey=cert.key&tlscert=cert.crt" -v --loglevel debug
```

Example certificates can be taken from `quiche\apps\src\bin\` or generated using OpenSSL.

## QUICHE CC Tests

### FAQ

#### What is the best way to trace the amount of packets sent?

qlog isn't recommended if you're testing performance. Best option there is to record a tcpdump/wireshark trace and then later convert that to qlog. This is something qvis (https://qvis.edm.uhasselt.be/) can do automatically or you can use the pcap2qlog tool yourself (https://github.com/quiclog/pcap2qlog). Caveat: this automatic conversion is potentially a little outdated. If you encounter issues, let me know (I maintain both tools). qvis has a "statistics" tab that shows you high-level things like amount of packets received/sent etc. [@lucasp](https://quicdev.slack.com/team/U68ALS6BB) might explain how you can export the TLS keys from quiche to decrypt the .pcap?

You can set the envvar `RUST_LOG=info` and quiche-client or server will print some very basic stats e.g. `2020-05-13T14:38:48.156718365Z INFO  quiche_client] connection closed, recv=15 sent=10 lost=0 rtt=19.594587ms cwnd=14520 delivery_rate=8265`

```
QLOGDIR=/path/to/folder` can be used if you built the binary using `--features qlog
```

`SSLKEYLOGFILE=/path/to/file.keys` will dump out TLS keys

[TLS in Wireshark](https://wiki.wireshark.org/TLS)

## Experiment

### Server

```
cd projects/quic/quiche/tools/apps/
Max-McBook-Pro:apps maxsharabayko$ ./target/release/quiche-client --body ./genfile_100MB.txt --no-verify http://192.168.1.56:4433/genfile_100MB.txt

```


Server sends a file

```
./target/release/quiche-server --listen 0.0.0.0:4433 --root ./ --cc-algorithm reno
```



Client receives file transfer.

```
./target/release/quiche-client --no-verify http://192.168.1.237:4433/genfile_100MB.txt --dump-responses ./rcv --disable-hystart
```



