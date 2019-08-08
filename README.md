# Requirements

* cmake (as build system)
* OpenSSL
* Pthreads (use pthreads4w on Windows)

```
git submodule init
git submodule update
mkdir _build && cd _build
cmake ../
cmake --build ./
```
## Example on Windows

```
cmake ../ -G"Visual Studio 16 2019" -A x64 -DPTHREAD_INCLUDE_DIR=C:\pthread-x64\include -DPTHREAD_LIBRARY=C:\pthread-x64\lib\pthread_lib.lib
```

# Example Usage

## Test Flow CC

### Sender
```
srt-xtransmit generate "srt://127.0.0.1:4200?transtype=file&messageapi=1&payloadsize=1456&congestion=flow&rcvbuf=1000000000&sndbuf=1000000000&fc=800000" --msgsize 1456 --num 1000 --statsfile stats-snd.csv --statsfreq 1s
```

### Receiver
```
srt-xtransmit receive "srt://:4200?transtype=file&messageapi=1&payloadsize=1456&congestion=flow&rcvbuf=1000000000&sndbuf=1000000000&fc=800000" --msgsize 1456  --statsfile stats-rcv.csv --statsfreq 1s
```

## Generate with live mode

### Send
```
srt-xtransmit generate "srt://127.0.0.1:4200?transtype=live&messageapi=1&payloadsize=1456" --msgsize 1456 --num -1 --sendrate 6Mbps
```

### Receive
```
srt-xtransmit receive "srt://:4200?transtype=live&messageapi=1&payloadsize=1456" --msgsize 1456
```
or with `srt-test-messaging` app:
```
srt-test-messaging "srt://:4200?rcvbuf=12058624&smoother=live" -reply 0 -msgsize 1456 -printmsg 1
```
