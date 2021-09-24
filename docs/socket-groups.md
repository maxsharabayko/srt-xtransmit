# SRT Socket Groups

## Build

To build `srt-xtransmit` with a support for SRT socket groups specify the `-DENABLE_EXPERIMENTAL_BONDING=ON`
CMake option. See an example below.

```shell
mkdir _build && cd _build
cmake ../ -DENABLE_EXPERIMENTAL_BONDING=ON
cmake --build ./
```

## Command-Line Syntax

### A Group of Callers

A group of callers should be listed as a space-separated list.

```
srt://<ip1>:<port1>?grouptype=<gt>&latency=<late>
```

### A Group of Receivers



## Examples


```shell
srt-xtransmit receive srt://:4200?groupconnect=1 --enable-metrics -v --reconnect
```

