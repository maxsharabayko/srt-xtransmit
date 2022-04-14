from collections import namedtuple
import pathlib

import bokeh.io
import bokeh.layouts as layouts
import bokeh.models as models
import bokeh.plotting as plotting
import click
import pandas as pd


PLOT_WIDTH = 700
PLOT_HEIGHT = 300
TABLE_WIDTH = 900
TOOLS = 'pan,xwheel_pan,box_zoom,reset,save'
linedesc = namedtuple("linedesc", ['col', 'legend', 'color'])

STATS_LIST = [
    'Number of observations',
    'Min, us',
    'Max, us',
    'Mean, us',
    'Standard deviation, us',
    '25th percentile (Q1), us',
    '50th percentile (Median, Q2), us',
    '75th percentile (Q3), us',
    '90th percentile, us',
    '95th percentile, us',
    '99th percentile, us',
    'Interquartile range (IQR, Q3 - Q1), us',
]


class IsNotCSVFile(Exception):
    pass


def export_plot_png(export_png, plot, name, postfix):
    if export_png:
        # The following two lines remove toolbar from PNG
        plot.toolbar.logo = None
        plot.toolbar_location = None
        bokeh.io.export_png(plot, filename=f'{name}-{postfix}.png')


def get_stats(s: pd.Series):
    """ Calculate basic sample `s` statistics. """
    q1 = round(s.quantile(0.25), 2)
    median = round(s.median(), 2)
    q3 = round(s.quantile(0.75), 2)
    p90 = round(s.quantile(0.90), 2)
    p95 = round(s.quantile(0.95), 2)
    p99 = round(s.quantile(0.99), 2)
    iqr = round(q3 - q1, 2)
    mean = round(s.mean(), 2)
    std = round(s.std(), 2)
    minimum = round(s.min(), 2)
    maximum = round(s.max(), 2)
    n = len(s)
    return [n, minimum, maximum, mean, std, q1, median, q3, p90, p95, p99, iqr]


def create_plot(title, xlabel, ylabel, source, lines, yformatter=None):
    fig = plotting.figure(
        plot_width=PLOT_WIDTH,
        plot_height=PLOT_HEIGHT,
        tools=TOOLS
    )
    fig.title.text = title
    fig.xaxis.axis_label = xlabel
    fig.yaxis.axis_label = ylabel

    fig.xaxis.formatter = models.NumeralTickFormatter(format='0,0')
    if yformatter is not None:
        fig.yaxis.formatter = yformatter

    is_legend = False
    for x in lines:
        if x.legend != '':
            is_legend = True
            fig.line(x='sTime', y=x.col, color=x.color, legend_label=x.legend, source=source)
        else:
            fig.line(x='sTime', y=x.col, color=x.color, source=source)

    if is_legend:
        fig.legend.click_policy="hide"

    return fig


def create_packets_plot(source):
    lines = [
        linedesc('pktReceivedInInterval', 'Received', 'green'),
        linedesc('pktLostInInterval', 'Lost', 'red'),
        linedesc('pktReorderedInInterval', 'Reordered', 'blue')
    ]

    return create_plot(
        'Packets',
        'Time (s)',
        'Number of Packets',
        source,
        lines,
        models.NumeralTickFormatter(format='0,0')
    )


def create_latency_plot(source):
    lines = [
        linedesc('msLatencyMin', 'Min', 'blue'),
        linedesc('msLatencyMax', 'Max', 'red'),
        linedesc('msLatencyAvg', 'Smoothed', 'green')
    ]

    return create_plot(
        'Transmission Delay (System Clock Delta)',
        'Time (s)',
        'Delay (ms)',
        source,
        lines,
        models.NumeralTickFormatter(format='0,0')
    )


def create_jitter_plot(source):
    lines = [
        linedesc('usDelayFactor', 'TS-DF', 'red'),
        linedesc('usJitter', 'Jitter', 'green'),
    ]

    return create_plot(
        'Time-Stamped Delay Factor (TS-DF) vs Interarrival Jitter (RFC 3550)',
        'Time (s)',
        'Jitter (us)',
        source,
        lines,
        models.NumeralTickFormatter(format='0,0')
    )


@click.command()
@click.argument(
    'metrics_filepath',
    type=click.Path(exists=True)
)
@click.option(
    '--export-png',
    is_flag=True,
    default=False,
    help='Export plots to .png files.',
    show_default=True
)
def plot_metrics(metrics_filepath, export_png):
    """
    This script processes a .csv file with metrics produced by the
    srt-xtransmit application and visualizes the data.
    """
    filepath = pathlib.Path(metrics_filepath)
    filename = filepath.name

    if not filename.endswith('.csv'):
        raise IsNotCSVFile(f'{filepath} does not correspond to a .csv file')

    name, _ = filename.rsplit('.', 1)
    name_parts = name.split('-')
    html_filename = name + '.html'
    html_filepath = filepath.parent / html_filename

    # Prepare data
    df = pd.read_csv(filepath)

    df['Timepoint'] = pd.to_datetime(df['Timepoint'])
    df['Time'] = df['Timepoint'] - df['Timepoint'].iloc[0]
    df['sTime'] = df['Time'].dt.total_seconds()

    df['pktReceivedInInterval'] = df['pktReceived'].diff()
    df['pktLostInInterval'] = df['pktLost'].diff()
    df['pktReorderedInInterval'] = df['pktReordered'].diff()

    df['msLatencyMin'] = df['usLatencyMin'] / 1000
    df['msLatencyMax'] = df['usLatencyMax'] / 1000
    df['msLatencyAvg'] = df['usLatencyAvg'] / 1000
    
    # A list for storing plots
    plots = []
    src = models.ColumnDataSource(df)

    fig_packets = create_packets_plot(src)
    fig_latency = create_latency_plot(src)
    fig_jitter = create_jitter_plot(src)
    plots.append(fig_packets)
    plots.append(fig_latency)
    plots.append(fig_jitter)

    # Table: Latency Statistics
    latency_stats = {}
    latency_stats['stats'] = STATS_LIST
    latency_stats['min'] = get_stats(df['msLatencyMin'])
    latency_stats['max'] = get_stats(df['msLatencyMax'])
    latency_stats['smoothed'] = get_stats(df['msLatencyAvg'])

    latency_source = models.ColumnDataSource(pd.DataFrame(latency_stats))
    latency_columns = [
        models.widgets.TableColumn(field='stats', title='Statistic'),
        models.widgets.TableColumn(field='min', title='Min'),
        models.widgets.TableColumn(field='max', title='Max'),
        models.widgets.TableColumn(field='smoothed', title='Smoothed'),
    ]
    latency_table = models.widgets.DataTable(
        columns=latency_columns,
        source=latency_source,
        width=TABLE_WIDTH
    )

    # Table: Jitter Statistics
    jitter_stats = {}
    jitter_stats['stats'] = STATS_LIST
    jitter_stats['delay_factor'] = get_stats(df['usDelayFactor'])
    jitter_stats['jitter'] = get_stats(df['usJitter'])

    jitter_source = models.ColumnDataSource(pd.DataFrame(jitter_stats))
    jitter_columns = [
        models.widgets.TableColumn(field='stats', title='Statistic'),
        models.widgets.TableColumn(field='delay_factor', title='TS-DF'),
        models.widgets.TableColumn(field='jitter', title='Jitter'),
    ]
    jitter_table = models.widgets.DataTable(
        columns=jitter_columns,
        source=jitter_source,
        width=TABLE_WIDTH
    )

    # Output to static .html file
    plotting.output_file(html_filepath, title="SRT Metrics Visualization")

    # Synchronize x-ranges of figures
    last_fig = plots[-1]

    for fig in plots:
        if fig is None:
            continue
        fig.x_range = last_fig.x_range

    # Show the results
    grid = layouts.gridplot(
        [
            [fig_packets, None],
            [fig_latency, latency_table],
            [fig_jitter, jitter_table]
        ]
    )

    plotting.show(grid)

if __name__ == '__main__':
    plot_metrics()
