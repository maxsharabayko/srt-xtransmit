# srt-xtransmit-prometheus

`srt-xtransmit-prometheus` is a fork of [srt-xtransmit](https://github.com/maxsharabayko/srt-xtransmit) with an integrated Prometheus exporter for SRT socket statistics.

It retains the original SRT/UDP testing functionality of `srt-xtransmit` and adds a native HTTP `/metrics` endpoint. SRT statistics are read directly from the active SRT sockets without using CSV files or an external exporter.

The Prometheus exporter is currently integrated into the live transmission commands:

* `receive` — Prometheus metrics for the input SRT socket
* `generate` — Prometheus metrics for the output SRT socket
* `route` — separate Prometheus exporters for input and output SRT sockets

The exporter exposes accumulated, interval-based and instantaneous SRT statistics directly from `SRT_TRACEBSTATS`.

## Prometheus Exporter

### Port selection

By default, the Prometheus exporter uses the same **port number** as the corresponding SRT endpoint.

SRT uses UDP while the Prometheus HTTP exporter uses TCP, so both can use the same numeric port simultaneously.

Example:

```text
SRT receiver:        UDP/4200
Prometheus exporter: TCP/4200
```

For a receiver:

```bash
srt-xtransmit-prometheus receive "srt://:4200"
```

Prometheus metrics are then available at:

```text
http://<host>:4200/metrics
```

The HTTP port can be overridden with:

```text
--stats-input-port PORT
--stats-output-port PORT
```

### Receive

Start an SRT listener:

```bash
srt-xtransmit-prometheus receive "srt://:4200"
```

The exporter automatically listens on TCP port 4200:

```bash
curl http://127.0.0.1:4200/metrics
```

To use a different Prometheus port:

```bash
srt-xtransmit-prometheus receive \
    "srt://:4200" \
    --stats-input-port 11001
```

This results in:

```text
SRT input:           UDP/4200
Prometheus exporter: TCP/11001
```

### Generate

Example SRT generator:

```bash
srt-xtransmit-prometheus generate \
    -o "srt://192.168.2.121:4200" \
    --sendrate 10Mbps
```

By default:

```text
SRT output:          UDP/4200
Prometheus exporter: TCP/4200
```

To override the exporter port:

```bash
srt-xtransmit-prometheus generate \
    -o "srt://192.168.2.121:4200" \
    --sendrate 10Mbps \
    --stats-output-port 11002
```

### Route

A route can expose input and output SRT statistics independently:

```bash
srt-xtransmit-prometheus route \
    -i "srt://:4200" \
    -o "srt://192.168.2.121:4300"
```

By default this creates:

```text
Input SRT:           UDP/4200
Input Prometheus:    TCP/4200

Output SRT:          UDP/4300
Output Prometheus:   TCP/4300
```

The ports can be explicitly configured:

```bash
srt-xtransmit-prometheus route \
    -i "srt://:4200" \
    -o "srt://192.168.2.121:4300" \
    --stats-input-port 11001 \
    --stats-output-port 11002
```

The metrics are then available at:

```text
http://<host>:11001/metrics   # input
http://<host>:11002/metrics   # output
```

Input and output exporters must use different TCP ports. If automatic port selection results in the same TCP port for both sides, `srt-xtransmit-prometheus` exits with an error and requires `--stats-input-port` and/or `--stats-output-port`.

### Direction labels

SRT socket metrics contain a `direction` label:

```text
direction="input"
direction="output"
```

Example:

```text
srt_mbps_recv_rate{direction="input",socket_id="650235235"} 10.33
srt_pkt_rcv_loss_total{direction="input",socket_id="650235235"} 0

srt_mbps_send_rate{direction="output",socket_id="828055493"} 10.34
srt_pkt_retrans_total{direction="output",socket_id="828055493"} 0
```

Each active SRT connection is additionally identified by its SRT `socket_id`.

### Connection state

The exporter remains available even when no SRT connection is currently established.

For example:

```text
srt_xtransmit_prometheus_up 1
srt_active_connections{direction="input"} 0
```

After a connection is established:

```text
srt_active_connections{direction="input"} 1
srt_socket_up{direction="input",socket_id="650235235"} 1
```

### Metrics

The exporter exposes SRT statistics including:

* packet and byte counters
* unique packets and bytes
* packet loss
* retransmissions
* ACK and NAK statistics
* send and receive bitrate
* RTT
* estimated bandwidth
* congestion and flow windows
* packets in flight
* send and receive buffer levels
* TSBPD delays
* reorder statistics
* belated packets
* packet-filter/FEC statistics
* dropped packets and bytes
* undecryptable packets and bytes

Accumulated SRT statistics use Prometheus `counter` types where appropriate.

Interval-based SRT statistics are exposed as `gauge` values because they may be reset by another SRT statistics request using `clear=1`.

Prometheus scrapes performed by the integrated exporter do **not** reset SRT statistics.

### Prometheus configuration

Example `prometheus.yml` for an SRT route using exporter ports 11001 and 11002:

```yaml
scrape_configs:
  - job_name: "srt-xtransmit-prometheus-input"
    scrape_interval: 1s
    static_configs:
      - targets:
          - "192.168.2.100:11001"

  - job_name: "srt-xtransmit-prometheus-output"
    scrape_interval: 1s
    static_configs:
      - targets:
          - "192.168.2.100:11002"
```

For a receiver using the default SRT/Prometheus port 4200:

```yaml
scrape_configs:
  - job_name: "srt-xtransmit-prometheus"
    scrape_interval: 1s
    static_configs:
      - targets:
          - "192.168.2.100:4200"
```

### Example PromQL

Current receiver bitrate as reported by SRT:

```promql
srt_mbps_recv_rate
```

Current sender bitrate:

```promql
srt_mbps_send_rate
```

Packet loss rate derived from the accumulated counter:

```promql
rate(srt_pkt_rcv_loss_total[10s])
```

Unique received payload bitrate derived from the accumulated byte counter:

```promql
rate(srt_byte_recv_unique_total[10s]) * 8
```

### UDP endpoints

Prometheus SRT statistics are only created for SRT endpoints.

A pure UDP endpoint such as:

```text
udp://:4200
```

does not have SRT socket statistics and therefore does not create an SRT Prometheus exporter for that endpoint.

---

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

[![Ubuntu-18.04](https://github.com/maxsharabayko/srt-xtransmit/actions/workflows/ubuntu.yml/badge.svg)](https://github.com/maxsharabayko/srt-xtransmit/actions/workflows/ubuntu.yml)
[![MacOS](https://github.com/maxsharabayko/srt-xtransmit/actions/workflows/macos-ccpp.yml/badge.svg)](https://github.com/maxsharabayko/srt-xtransmit/actions/workflows/macos-ccpp.yml)
[![CodeFactor](https://www.codefactor.io/repository/github/maxsharabayko/srt-xtransmit/badge)](https://www.codefactor.io/repository/github/maxsharabayko/srt-xtransmit)
[![LGTM alerts](https://img.shields.io/lgtm/alerts/g/maxsharabayko/srt-xtransmit.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/maxsharabayko/srt-xtransmit/alerts/)

### Requirements

* C++14 compliant compiler (GCC 4.8+, CLang, MSVC, etc...)
* cmake (as a build configuration system)
* OpenSSL (for encryption - required by SRT)

**Note!** In order to have absolute timepoint in CSV statistics, GCC v5.0 (instead of v4.8+) and above is required
(with support for [std::put_time](https://en.cppreference.com/w/cpp/io/manip/put_time)).

### Building on Linux/Mac

#### 1. Create the directory for the project and clone the source code in here

```shell
mkdir -p projects/srt/srt-xtransmit
cd projects/srt
git clone --recurse-submodules \
    https://github.com/skureal/srt-xtransmit-prometheus.git

cd srt-xtransmit-prometheus
```

#### 2. Initialize, fetch and checkout submodules

```shell
cd srt-xtransmit
git submodule update --init --recursive
```

<!-- https://git-scm.com/book/en/v2/Git-Tools-Submodules -->

**Tip:** There is a simpler method for doing the above. If you pass `--recurse-submodules` to the `git clone` command, it will automatically initialize and update each submodule in the repository, including nested submodules if any of the submodules in the repository have submodules themselves.

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

### Docker build

#### Requirements

* Docker engine - [installation guide](https://docs.docker.com/engine/install/)

#### Build Alpine docker image
```shell
docker build --rm -f docker/Dockerfile.alpine -t srt-xtransmit-alpine:latest .
```

#### Build Ubuntu docker image
```shell
docker build --rm -f docker/Dockerfile.ubuntu -t srt-xtransmit-ubuntu:latest .
```

#### Build with different srt lib version
```shell
docker build --rm -f docker/Dockerfile.alpine --build-arg srt_version="v1.4.0" -t srt-xtransmit-alpine:srt1.4.0 .
```
* srt_version - branch name or commit

#### Build with specififc build options
```shell
docker build --rm -f docker/Dockerfile.alpine --build-arg build_options="-DENABLE_CXX17=ON -DENABLE_BONDING=ON" -t srt-xtransmit-alpine:bonding .
```
* build_options - list of build options

### Building with Nix

If you have [Nix](https://nixos.org/download/) installed with flakes enabled, you can build directly from the repository:

```shell
nix build .#srt-xtransmit
```

The binary will be available at `./result/bin/srt-xtransmit`.

To run without installing:

```shell
nix shell .#srt-xtransmit -c srt-xtransmit --version
```

### Switching SRT version

Before building the project with cmake, checkout the desired SRT library version.

After git submodules are initialized:

```shell
git submodule update --init -- recursive
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

