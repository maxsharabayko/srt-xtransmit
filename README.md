# srt-xtransmit

TODO: Brief description


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

[![Actions Status](https://github.com/maxsharabayko/srt-xtransmit/workflows/C/C++%20CI/badge.svg)](https://github.com/maxsharabayko/srt-xtransmit/actions)
[![CodeFactor](https://www.codefactor.io/repository/github/maxsharabayko/srt-xtransmit/badge)](https://www.codefactor.io/repository/github/maxsharabayko/srt-xtransmit)

## Requirements

* C++14 compliant compiler (GCC 4.8+)
* cmake (as a build configuration system)
* OpenSSL (for encryption - required by SRT)
* Pthreads (use pthreads4w on Windows - required by SRT)

## Building on Linux/Mac

### 1. Create the directory for the project and clone the source code in here
```
mkdir -p projects/srt/srt-xtransmit
cd projects/srt
git clone https://github.com/maxsharabayko/srt-xtransmit.git srt-xtransmit
```

### 2. Initialize, fetch and checkout submodules
```
cd srt-xtransmit
git submodule init
git submodule update
```

<!-- https://git-scm.com/book/en/v2/Git-Tools-Submodules -->

**Tipp:** There is another way to do this which is a little simpler, however. If you pass `--recurse-submodules` to the `git clone` command, it will automatically initialize and update each submodule in the repository, including nested submodules if any of the submodules in the repository have submodules themselves.

**Tipp:** If you already cloned the project and forgot `--recurse-submodules`, you can combine the `git submodule init` and `git submodule update` steps by running `git submodule update --init`. To also initialize, fetch and checkout any nested submodules, you can use the foolproof `git submodule update --init --recursive`.

### 3. Install submodules dependencies, in particular, [SRT library](https://github.com/Haivision/srt) dependencies

Install CMake dependencies and set the environment variables for CMake to find openssl:

**Ubuntu 18.04**
```
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install tclsh pkg-config cmake libssl-dev build-essential
```

**CentOS**

TODO

**MacOS**
```
brew install cmake
brew install openssl
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
export OPENSSL_LIB_DIR=$(brew --prefix openssl)"/lib"
export OPENSSL_INCLUDE_DIR=$(brew --prefix openssl)"/include"
```

On macOS, you may also need to set

export PKG_CONFIG_PATH="/usr/local/opt/openssl/lib/pkgconfig"
for pkg-config to find openssl. Run `brew info openssl` to check the exact path.

### 4. Build srt-xtransmit

**Ubuntu 18.04 / CentOS**
```
mkdir _build && cd _build
cmake ../
cmake --build ./
```

**MacOS**
```
mkdir _build && cd _build
cmake ../ -DENABLE_CXX17=OFF
cmake --build ./
```

## Building on Windows

TODO: Revise and update build instructions.

### Install Pre-requirements

1. Download [nuget CLI](https://www.nuget.org/downloads) to the `_build` folder, created in the steps.
2. Install [OpenSSL](http://slproweb.com/download/Win64OpenSSL_Light-1_1_1c.exe)

### Build the Project
```
git clone https://github.com/maxsharabayko/srt-xtransmit.git srt-xtransmit
cd srt-xtransmit
git submodule init
git submodule update
mkdir _build && cd _build
nuget install cinegy.pthreads-win64 -version 2.9.1.17
cmake ../ -G"Visual Studio 16 2019" -A x64 -DPTHREAD_INCLUDE_DIR="_build\cinegy.pthreads-win64.2.9.1.17\sources" -DPTHREAD_LIBRARY="$(SolutionDir)cinegy.pthreads-win64.2.9.1.17\runtimes\win-x64\native\release\pthread_lib.lib"
cmake --build ./
```

## Switching SRT version

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

Send all files in folder "srcfolder", and  receive into a current folder "./".

Requires C++17 compliant compiler with support for `file_system` (GCC 8 and higher). See [compiler support](https://en.cppreference.com/w/cpp/compiler_support) matrix. \
Build with `-DENABLE_CXX17=ON` build option to enable (default -DENABLE_CXX17=OFF).

### Sender
```
srt-xtransmit file send srcfolder/ "srt://127.0.0.1:4200" --statsfile stats-snd.csv --statsfreq 1s
```

### Receiver
```
srt-xtransmit file receive "srt://:4200" ./ --statsfile stats-rcv.csv --statsfreq 1s
```