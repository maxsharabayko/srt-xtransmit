# SRT Socket Groups

## Build

To build `srt-xtransmit` with support for SRT socket groups specify the
`-DENABLE_EXPERIMENTAL_BONDING=ON` CMake option. See an example below.

```shell
mkdir _build && cd _build
cmake ../ -DENABLE_EXPERIMENTAL_BONDING=ON
cmake --build ./
```

## Command-Line Syntax

### A Group of Callers

A group of callers should be listed as a space-separated list or URIs in the following format:

```shell
srt://<ip1>:<port1>?grouptype=<gt>&weight=<w>&latency=<late>&<option>=<value>...
```

where

- `grouptype` specifies group connection type: `broadcast` (default) or `backup` (should be set only for the first URI in a group);
- `weight` defines member socket weight in the main/backup connection mode (higher weight means higher priority); ignored in broadcast configuration;
- other socket options.

Only the first URI in the list has to specify the group connection type. Options specified for the first member (in the first URI)
are inherited by other group members.
The exception is the `weight` option, which is only set for those members for which it is provided.

Example URI command-line for main/backup connection:

```shell
srt://<ip1>:<port1>?grouptype=backup&weight=10&latency=120 srt://<ip2>:<port2>?weight=9
```

### A Group of Listeners

A single listening socket bound to a specific UDP port can accept group connections.
By default, accepting group connections is not allowed and must be enabled by `groupconnect=1`.

```shell
srt://:<port1>?groupconnect=1
```

Binding a listener to a specific NIC and port can be done with `bind=<ip>:<port>` query:

```shell
srt://:<port1>?bind=<ip1>&groupconnect=1
```

or explicitly defining the connection method:

```shell
srt://<ip1>:<port1>?mode=listener&groupconnect=1
```

To listen on several UDP ports for a group connection, several listeners must be initialized.
If more than one listener is specified, there is no need to set `groupconnect=1`, it is done automatically.

```shell
srt://:<port1>?bind=<ip1> srt://<ip2>:<port2>?mode=listener
```

## Receive and Generate Examples

On the listener side, the `--reconnect` flag must be provided to allow further connections
after the initial connection is established. Otherwise, the listener will be closed after the first member gets connected.

On the caller side, the `--reconnect` flag enables reconnection attempts for those members which
might have been disconnected in run time. For example, if the main link got broken and disconnected,
reconnection attempts will be made with an interval of 1 s.

```shell
(receive)
srt-xtransmit receive srt://<ip1>:<port1>?groupconnect=1&mode=listener --enable-metrics --reconnect

(generate)
srt-xtransmit generate srt://<ip1>:<port1>?bind=<nic1> srt://<ip1>:<port1>?bind=<nic2> --enable-metrics --reconnect
```

Connecting to several ports.

```shell
(receive)
srt-xtransmit receive srt://:<port1>?latency=200 srt://:<port2> --enable-metrics --reconnect

(generate)
srt-xtransmit generate srt://<ip1>:<port1>?grouptype=backup&weight=2 srt://<ip1>:<port2>?weight=1 --enable-metrics --reconnect
```

## Route Examples

```shell
(group listener-receiver routing to group caller-sender)
srt-xtransmit route -i srt://<ip1>:<port1>?mode=listener srt://<ip2>:<port2>?mode=listener \
                    -o srt://<ip3>:<port3>?grouptype=backup srt://<ip4>:<port4>
```
