# Requirements

* cmake (as build system)
* OpenSSL
* Pthreads (use pthreads4w on Windows)

# Build Instructions

## Linux

```
git submodule init
git submodule update
mkdir _build && cd _build
cmake ../
cmake --build ./
```
## Windows

### Install Pre-requirements

1. Download [nuget CLI](https://www.nuget.org/downloads) to the `_build` folder, created in the steps.
2. Install [OpenSSL](http://slproweb.com/download/Win64OpenSSL_Light-1_1_1c.exe)

### Build the Project
```
git clone https://stash.haivision.com/scm/~maxsharabayko/srt-xtransmit.git srt-xtransmit
cd srt-xtransmit
git submodule init
git submodule update
mkdir _build && cd _build
nuget install cinegy.pthreads-win64 -version 2.9.1.17
cmake ../ -G"Visual Studio 16 2019" -A x64 -DPTHREAD_INCLUDE_DIR="_build\cinegy.pthreads-win64.2.9.1.17\sources" -DPTHREAD_LIBRARY="$(SolutionDir)cinegy.pthreads-win64.2.9.1.17\runtimes\win-x64\native\release\pthread_lib.lib"
cmake --build ./
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
