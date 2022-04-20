## Measuring the End-to-end Transmission Delay

Using `generate` and `receive` subcommands of `srt-xtransmit` the following metrics can be measured for SRT and/or UDP streaming:

- end-to-end latency (transmission delay), refer to the documentation on [SRT Latency](https://maxsharabayko.github.io/srtcookbook/protocol/tsbpd/latency/);
- packet reordering
- packet loss;
- Jitter.

### Sender (Payload Generator)

On the sending side the `generate` subcommand with `--enable-metrics` command-line flag should be used. The `--enable-metrics` flag enables payload generation mode when additional supplemental data is written in the payload of every packet. Using this additional information the receiver then can calculate metrics for each packet.

```shell
set ip=127.0.0.1
set port=4200
set latency=120

srt-xtransmit generate "srt://%ip%:%port%?latency=%latency%" --enable-metrics --sendrate 5Mbps --duration 120s
```

Command line options used:

- `--enable-metrics` tells the generator to use a certain payload format with supplemental information.
- `--sendrate <value>` defines the packet generation and sending rate;
- `--duration <value>` defines the duration of transmission.

**Note** that both SRT and UDP can be used as transmission medium.

### Receiver (Payload Analyzer)

Then receiver receives the packet, reads the timestamp and sequential packet number from the payload, compares to its local system time and calculates the metrics. This behaviour is turned on by specifying the `--enable-metrics` flag, but it is assumed the payload generator prepared a certain payload for the receiever.

```shell
set ip=
set port=4200
set latency=120
set metricsfile="metrics.csv"
set metricsfreq=1s

srt-xtransmit receive "srt://%ip%:%port%?latency=%latency%" --enable-metrics --metricsfile %metricsfile% --metricsfreq %metricsfreq%
```

Command line options used:

- `--enable-metrics` tells the receiver to expect a certain payload format and analyse it.
- `--metricsfile <value>` output CSV file that will contain the collected metrics
- `--metricsfreq <value>` frequency of retrieving the metrics (every X milliseconds).

**Note** that both SRT and UDP can be used as transmission medium.

### Example 1. Sender and Receiver on Different Machines

When sender and receiver are located on different machines, they have different clocks. Therefore, unless the clocks are well synchronised, the value of latency also includes the difference in clocks, and can be used to monitor changes of the end-to-end latency.

### Example 2. Sender and Receiver on the Same Machine

Both sender and the final receiver of the payload can be located on the same machine. Then the exact transmission delay can be measured.

Consider the following example, where both payload generator and payload receiver are located on the machine B, while the transmission itself goes from A to B and then back to B. In this case, the A->B->A transmission characteristics could be measured.

```
A:     SRT -> SRT
       ^        \
      /          \
     /            v
B:  SRT          SRT
  generate     receive
```

