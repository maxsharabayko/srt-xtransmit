# Requirements

* cmake (as a build configuration system)
* OpenSSL (for encryption - required by SRT)
* Pthreads (use pthreads4w on Windows - required by SRT)

# Functionality

## Commands

* **generate** -  dummy content streaming over SRT for performance tests
* **receive** - receiving SRT streaming to null for performance tests
* **forward** - forward packets bidirectionally between two SRT connections
* **file send** - segment-based file/folder sender (C++17)
* **file receive** - segment-based file/folder receiver (C++17)

## Common

Collecting SRT statistics in CSV format.

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

# Switching SRT version

Before building the project with cmake, checkout the desired SRT library version.

After 
```
git submodule init
git submodule update
```
go to srt submodule and checkout, e.g. v1.3.4

```
cd submodule/srt
git checkout v1.3.4
```


# Example Use Cases

## Test Live Transfer Performance

### Sender

```
srt-xtransmit generate "srt://127.0.0.1:4200?transtype=live&rcvbuf=1000000000&sndbuf=1000000000" --msgsize 1316 --sendrate 15Mbps --duration 10s --statsfile stats-snd.csv --statsfreq 100ms
```

### Receiver

```
srt-xtransmit receive "srt://:4200?transtype=live&rcvbuf=1000000000&sndbuf=1000000000" --msgsize 1316 --statsfile stats-rcv.csv --statsfreq 100ms
```

## Test File CC Performance

### Sender

```
srt-xtransmit generate "srt://127.0.0.1:4200?transtype=file&messageapi=1&payloadsize=1456&rcvbuf=1000000000&sndbuf=1000000000&fc=800000" --msgsize 1456 --num 1000 --statsfile stats-snd.csv --statsfreq 1s
```

### Receiver

```
srt-xtransmit receive "srt://:4200?transtype=file&messageapi=1&payloadsize=1456&rcvbuf=1000000000&sndbuf=1000000000&fc=800000" --msgsize 1456  --statsfile stats-rcv.csv --statsfreq 1s
```

## 

## Transmit File/Folder

Requires C++17 compliant compiler. Build with `-DENABLE_CXX17` build option (enabled by default).

Send all files in folder "srcfolder", and  receive into a current folder "./".

### Sender
```
srt-xtransmit file send srcfolder/ "srt://127.0.0.1:4200" --statsfile stats-snd.csv --statsfreq 1s
```

### Receiver
```
srt-xtransmit file receive "srt://:4200" ./ --statsfile stats-rcv.csv --statsfreq 1s
```

