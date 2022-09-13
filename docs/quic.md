# QUIC Socket


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

Note that to send a datagram the payload size should be set to some value around 1100 bytes.

```shell
(client server)
srt-xtransmit.exe generate "quic://127.0.0.1:4200" --sendrate 1Mbps --duration 10s --msgsize 1000 --enable-metrics

(server receiver)
srt-xtransmit receive "quic://:4433?tlskey=cert.key&tlscert=cert.crt" -v
```



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



