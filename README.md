# srt-xtransmit

`srt-xtransmit` is a testing utility with support for SRT and UDP network protocols.

## Functionality

### Live Transmission Commands

* **generate** -  dummy content streaming over SRT for performance tests
* **receive** - receiving SRT streaming to null for performance tests
* **route** - route packets between two sockets (UDP/SRT) uni- or bidirectionally

### File Transmission Commands

* **file send** - segment-based file/folder sender (requires C++17: `-DENABLE_CXX17=ON`)
* **file receive** - segment-based file/folder receiver (requires C++17: `-DENABLE_CXX17=ON`)
* **file forward** - forward packets bidirectionally between two SRT connections (requires C++17: `-DENABLE_CXX17=ON`)

## Build Instructions

[![Actions Status](https://github.com/maxsharabayko/srt-xtransmit/workflows/C/C++%20CI/badge.svg)](https://github.com/maxsharabayko/srt-xtransmit/actions)
[![CodeFactor](https://www.codefactor.io/repository/github/maxsharabayko/srt-xtransmit/badge)](https://www.codefactor.io/repository/github/maxsharabayko/srt-xtransmit)
[![LGTM alerts](https://img.shields.io/lgtm/alerts/g/maxsharabayko/srt-xtransmit.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/maxsharabayko/srt-xtransmit/alerts/)

### Requirements

* C++14 compliant compiler (GCC 4.8+, CLang, MSVC, etc...)
* cmake (as a build configuration system)
* OpenSSL (for encryption - required by SRT)

**Note!** In order to have absolute timepoint in CSV statistics GCC v5.0 (instead of v4.8+) and above is required
(with support for [std::put_time](https://en.cppreference.com/w/cpp/io/manip/put_time)).

### Building on Linux/Mac

#### 1. Create the directory for the project and clone the source code in here

```shell
mkdir -p projects/srt/srt-xtransmit
cd projects/srt
git clone https://github.com/maxsharabayko/srt-xtransmit.git srt-xtransmit
```

#### 2. Initialize, fetch and checkout submodules

```shell
cd srt-xtransmit
git submodule init
git submodule update
```

<!-- https://git-scm.com/book/en/v2/Git-Tools-Submodules -->

**Tip:** There is another way to do this which is a little simpler, however. If you pass `--recurse-submodules` to the `git clone` command, it will automatically initialize and update each submodule in the repository, including nested submodules if any of the submodules in the repository have submodules themselves.

**Tip:** If you already cloned the project and forgot `--recurse-submodules`, you can combine the `git submodule init` and `git submodule update` steps by running `git submodule update --init`. To also initialize, fetch and checkout any nested submodules, you can use the foolproof `git submodule update --init --recursive`.

#### 3. Install submodules dependencies, in particular, [SRT library](https://github.com/Haivision/srt) dependencies

Install CMake dependencies and set the environment variables for CMake to find openssl:

##### Ubuntu 18.04

```shell
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install tclsh pkg-config cmake libssl-dev build-essential
```

##### CentOS

TODO

##### MacOS

```shell
brew install cmake
brew install openssl
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
export OPENSSL_LIB_DIR=$(brew --prefix openssl)"/lib"
export OPENSSL_INCLUDE_DIR=$(brew --prefix openssl)"/include"
```

On macOS, you may also need to set

export PKG_CONFIG_PATH="/usr/local/opt/openssl/lib/pkgconfig"
for pkg-config to find openssl. Run `brew info openssl` to check the exact path.

#### 4. Build srt-xtransmit

##### Ubuntu 18.04 / CentOS

```shell
mkdir _build && cd _build
cmake ../
cmake --build ./
```

##### Building on MacOS

```shell
mkdir _build && cd _build
cmake ../ -DENABLE_CXX17=OFF
cmake --build ./
```

### Building on Windows

Comprehensive Windows build instructions can be found in the corresponding [wiki page](https://github.com/maxsharabayko/srt-xtransmit/wiki/Build-Instructions).

`vcpkg` package manager is the easiest way to build the OpenSSL dependency.

```shell
md _build && cd _build
cmake ../ -DENABLE_STDCXX_SYNC=ON
cmake --build ./
```

### Switching SRT version

Before building the project with cmake, checkout the desired SRT library version.

After git submodules are initialized:

```shell
git submodule init
git submodule update
```

go to srt submodule and checkout, e.g. v1.3.4

```shell
cd submodule/srt
git checkout v1.3.4
```

## Example Use Cases

### Test Live Transfer Performance

#### Sender

```shell
srt-xtransmit generate "srt://127.0.0.1:4200?transtype=live&rcvbuf=1000000000&sndbuf=1000000000" --msgsize 1316 --sendrate 15Mbps --duration 10s --statsfile stats-snd.csv --statsfreq 100ms
```

#### Receiver

```shell
srt-xtransmit receive "srt://:4200?transtype=live&rcvbuf=1000000000&sndbuf=1000000000" --msgsize 1316 --statsfile stats-rcv.csv --statsfreq 100ms
```

### Test File CC Performance

#### Sender

```shell
srt-xtransmit generate "srt://127.0.0.1:4200?transtype=file&messageapi=1&payloadsize=1456&rcvbuf=1000000000&sndbuf=1000000000&fc=800000" --msgsize 1456 --num 1000 --statsfile stats-snd.csv --statsfreq 1s
```

#### Receiver

```shell
srt-xtransmit receive "srt://:4200?transtype=file&messageapi=1&payloadsize=1456&rcvbuf=1000000000&sndbuf=1000000000&fc=800000" --msgsize 1456  --statsfile stats-rcv.csv --statsfreq 1s
```

### Transmit File/Folder

Send all files in folder "srcfolder", and  receive into a current folder "./".

Requires C++17 compliant compiler with support for `file_system` (GCC 8 and higher). See [compiler support](https://en.cppreference.com/w/cpp/compiler_support) matrix. \
Build with `-DENABLE_CXX17=ON` build option to enable (default -DENABLE_CXX17=OFF).

#### Sender

```shell
srt-xtransmit file send srcfolder/ "srt://127.0.0.1:4200" --statsfile stats-snd.csv --statsfreq 1s
```
#### Receiver

```shell
srt-xtransmit file receive "srt://:4200" ./ --statsfile stats-rcv.csv --statsfreq 1s
```

