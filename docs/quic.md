# QUIC Socket

## Build


## Command-Line Syntax

Example establish a QUIC connection and just try to maintain it.

```shell
()
srt-xtransmit receive "quic://127.0.0.1:4433" -v --loglevel debug

(receiver)
srt-xtransmit receive "quic://:4433?tlskey=cert.key&tlscert=cert.crt" -v --loglevel debug
```

Example certificates can be taken from `quiche\apps\src\bin\` or generated using OpenSSL.
