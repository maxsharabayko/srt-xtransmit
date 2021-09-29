from collections import namedtuple
import os
import pathlib

import bokeh.io
import bokeh.layouts as layouts
import bokeh.models as models
from bokeh.models.plots import _list_attr_splat
import bokeh.plotting as plotting
import click
import pandas as pd


PLOT_WIDTH = 700
PLOT_HEIGHT = 300
TOOLS = 'pan,xwheel_pan,box_zoom,reset,save'
linedesc = namedtuple("linedesc", ['col', 'legend', 'color'])


class IsNotCSVFile(Exception):
    pass


def export_plot_png(export_png, plot, name, postfix):
    if export_png:
        # The following two lines remove toolbar from PNG
        plot.toolbar.logo = None
        plot.toolbar_location = None
        bokeh.io.export_png(plot, filename=f'{name}-{postfix}.png')


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
            fig.line(x='Time', y=x.col, color=x.color, legend_label=x.legend, source=source)
        else:
            fig.line(x='Time', y=x.col, color=x.color, source=source)

    if is_legend:
        fig.legend.click_policy="hide"

    return fig


def create_packets_plot(source):
    lines = [
        linedesc('pktReceivedInInterval', 'Received', 'green'),
        linedesc('pktLostInInterval', 'Lost', 'brown'),
        linedesc('pktReorderedInInterval', 'Reordered', 'red')
    ]

    return create_plot(
        'Packets',
        'Time (ms)',
        'Number of Packets',
        source,
        lines,
        models.NumeralTickFormatter(format='0,0')
    )

def create_latency_plot(source):
    lines = [
        linedesc('usLatencyMin', 'Min Latency', 'green'),
        linedesc('usLatencyMax', 'Max Latency', 'red'),
        linedesc('usLatencyAvg', 'Avg Latency', 'black')
    ]

    return create_plot(
        'End-to-End Latency (System Clock Delta)',
        'Time (ms)',
        'Latency (μs)',
        source,
        lines,
        models.NumeralTickFormatter(format='0,0')
    )

def create_jitter_plot(source):
    lines = [
        linedesc('usJitter', 'Jitter', 'black')
    ]

    return create_plot(
        'Reading Jitter',
        'Time (ms)',
        'Jitter (μs)',
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
    This script processes a .csv file with metrics produced by
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
    df.Timepoint = pd.to_datetime(df.Timepoint)
    start_time = df.Timepoint[0]
    print(f'Start time: {start_time}')
    df['Time'] = df.Timepoint.apply(lambda row: row - start_time)
    print(df.Time)
    df['pktReceivedInInterval'] = df.pktReceived.diff()
    df['pktLostInInterval'] = df.pktLost.diff()
    df['pktReorderedInInterval'] = df.pktReordered.diff()
    
    # A dict for storing plots
    plots_array = []
    src = models.ColumnDataSource(df)
    plots_array.append(create_packets_plot(src))
    plots_array.append(create_latency_plot(src))
    plots_array.append(create_jitter_plot(src))
    
    # Output to static .html file
    plotting.output_file(html_filepath, title="SRT Metrics Visualization")

    # Synchronize x-ranges of figures
    last_fig = plots_array[-1]
    print(f'plots_array: {plots_array}')
    print(f'Last fig: {last_fig}')
    
    for fig in plots_array:
        if fig is None:
            continue
        fig.x_range = last_fig.x_range

    # Show the results
    grid = layouts.gridplot(
        [[el] for el in plots_array] 
    )
    plotting.show(grid)

if __name__ == '__main__':
    plot_metrics()
