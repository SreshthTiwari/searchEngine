import argparse
import csv
import os
import statistics
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
GRID = "#e4e3df"
ALGORITHM_COLOR = {"exhaustive": "#2a78d6", "wand": "#eb6834"}
FORMAT_STYLE = {
    "uncompressed": {"linestyle": "-", "marker": "o", "hatch": ""},
    "compressed": {"linestyle": "--", "marker": "s", "hatch": "///"},
}
SERIES_COLOR = {"uncompressed": "#2a78d6", "compressed": "#eb6834"}


def read_csv(path):
    if not os.path.exists(path):
        print("skipping missing file: %s" % path)
        return []
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def style_axes(ax, title, xlabel, ylabel):
    ax.set_facecolor(SURFACE)
    ax.set_title(title, loc="left", fontsize=12, color=INK, pad=12)
    ax.set_xlabel(xlabel, color=INK_SECONDARY, fontsize=10)
    ax.set_ylabel(ylabel, color=INK_SECONDARY, fontsize=10)
    ax.tick_params(colors=INK_SECONDARY, labelsize=9, length=0)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(GRID)


def new_figure(width=7.5, height=4.4):
    fig, ax = plt.subplots(figsize=(width, height), facecolor=SURFACE)
    return fig, ax


def save(fig, out_dir, name, caption):
    fig.text(0.01, 0.01, caption, fontsize=7.5, color=INK_SECONDARY, ha="left", va="bottom")
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    path = os.path.join(out_dir, name)
    fig.savefig(path, dpi=160, facecolor=SURFACE)
    plt.close(fig)
    print("wrote %s" % path)


def hardware_caption(rows):
    return rows[0]["hardware"] if rows and "hardware" in rows[0] else ""


def latency_by_algorithm(rows, out_dir):
    if not rows:
        return
    largest = max(int(r["dataset_size"]) for r in rows)
    rows = [r for r in rows if int(r["dataset_size"]) == largest]
    k = rows[0]["k"]
    fig, axes = plt.subplots(1, 2, figsize=(9, 4.4), facecolor=SURFACE, sharey=False)
    for ax, metric, label in ((axes[0], "p50_us", "median (p50)"), (axes[1], "p99_us", "tail (p99)")):
        style_axes(ax, "Query latency, %s" % label, "", "microseconds per query")
        formats = ["uncompressed", "compressed"]
        width = 0.36
        for offset, algorithm in ((-width / 2, "exhaustive"), (width / 2, "wand")):
            values = []
            for fmt in formats:
                match = [r for r in rows if r["index_format"] == fmt and r["algorithm"] == algorithm]
                values.append(float(match[0][metric]) if match else 0.0)
            positions = [i + offset for i in range(len(formats))]
            bars = ax.bar(positions, values, width=width * 0.92, color=ALGORITHM_COLOR[algorithm], label=algorithm)
            for bar, value in zip(bars, values):
                ax.text(bar.get_x() + bar.get_width() / 2, value, "%.0f" % value, ha="center", va="bottom",
                        fontsize=8.5, color=INK)
        ax.set_xticks(range(len(formats)))
        ax.set_xticklabels([f + " index" for f in formats])
        ax.legend(frameon=False, fontsize=9, labelcolor=INK_SECONDARY)
    fig.suptitle("Query latency by retrieval algorithm (%s documents, k=%s)" % (format(largest, ","), k),
                 x=0.01, ha="left", fontsize=13, color=INK)
    save(fig, out_dir, "1_query_latency_by_algorithm.png", hardware_caption(rows))


def index_size(rows, out_dir):
    if not rows:
        return
    row = rows[-1]
    fig, ax = new_figure(6.5, 4.4)
    style_axes(ax, "Index size before and after compression (%s documents)" % format(int(row["documents"]), ","),
               "", "megabytes")
    groups = [("Postings only", float(row["uncompressed_postings_mb"]), float(row["compressed_postings_mb"])),
              ("Whole index file", float(row["uncompressed_file_mb"]), float(row["compressed_file_mb"]))]
    width = 0.36
    for series, offset, idx in (("uncompressed", -width / 2, 1), ("compressed", width / 2, 2)):
        values = [g[idx] for g in groups]
        positions = [i + offset for i in range(len(groups))]
        bars = ax.bar(positions, values, width=width * 0.92, color=SERIES_COLOR[series],
                      hatch=FORMAT_STYLE[series]["hatch"], edgecolor=SURFACE, linewidth=0, label=series)
        for bar, value in zip(bars, values):
            ax.text(bar.get_x() + bar.get_width() / 2, value, "%.1f" % value, ha="center", va="bottom",
                    fontsize=9, color=INK)
    ax.set_xticks(range(len(groups)))
    ax.set_xticklabels([g[0] for g in groups])
    ax.legend(frameon=False, fontsize=9, labelcolor=INK_SECONDARY)
    ax.text(0.99, 0.97, "postings ratio %sx\n%s -> %s bytes/posting" % (
        row["compression_ratio"], row["uncompressed_bytes_per_posting"], row["compressed_bytes_per_posting"]),
        transform=ax.transAxes, ha="right", va="top", fontsize=9, color=INK_SECONDARY)
    save(fig, out_dir, "2_index_size.png", hardware_caption(rows))


def indexing_throughput(rows, out_dir):
    if not rows:
        return
    by_threads = defaultdict(list)
    for r in rows:
        by_threads[int(r["threads"])].append(float(r["docs_per_second"]))
    threads = sorted(by_threads)
    medians = [statistics.median(by_threads[t]) for t in threads]
    fig, ax = new_figure(7, 4.4)
    docs = format(int(rows[0]["documents"]), ",")
    style_axes(ax, "Indexing throughput by thread count (%s documents)" % docs, "worker threads", "documents per second")
    ax.plot(range(len(threads)), medians, color=SERIES_COLOR["uncompressed"], linewidth=2, marker="o", markersize=8,
            markeredgecolor=SURFACE, markeredgewidth=2, label="median of %d run(s)" % len(by_threads[threads[0]]))
    base = medians[0]
    for i, value in enumerate(medians):
        ax.text(i, value, "%s/s\n%.2fx" % (format(int(value), ","), value / base), ha="center", va="bottom",
                fontsize=8.5, color=INK)
    ax.set_xticks(range(len(threads)))
    ax.set_xticklabels([str(t) for t in threads])
    ax.set_ylim(0, max(medians) * 1.22)
    ax.legend(frameon=False, fontsize=9, labelcolor=INK_SECONDARY, loc="lower right")
    save(fig, out_dir, "3_indexing_throughput.png", hardware_caption(rows))


def series_chart(rows, x_key, x_label, title, name, out_dir, log_x):
    if not rows:
        return
    groups = defaultdict(list)
    for r in rows:
        groups[(r["index_format"], r["algorithm"])].append((int(r[x_key]), float(r["p50_us"])))
    fig, ax = new_figure(7.5, 4.6)
    style_axes(ax, title, x_label, "median query latency (microseconds)")
    for (fmt, algorithm), points in sorted(groups.items()):
        points.sort()
        style = FORMAT_STYLE[fmt]
        ax.plot([p[0] for p in points], [p[1] for p in points], color=ALGORITHM_COLOR[algorithm], linewidth=2,
                linestyle=style["linestyle"], marker=style["marker"], markersize=7, markeredgecolor=SURFACE,
                markeredgewidth=2, label="%s, %s" % (algorithm, fmt))
    if log_x:
        ax.set_xscale("log")
        xs = sorted({int(r[x_key]) for r in rows})
        ax.set_xticks(xs)
        ax.set_xticklabels([format(x, ",") for x in xs])
        ax.minorticks_off()
    else:
        xs = sorted({int(r[x_key]) for r in rows})
        ax.set_xticks(xs)
    ax.set_ylim(bottom=0)
    ax.legend(frameon=False, fontsize=9, labelcolor=INK_SECONDARY)
    save(fig, out_dir, name, hardware_caption(rows))


def main():
    parser = argparse.ArgumentParser(description="Generate benchmark charts from the CSV files in a results directory")
    parser.add_argument("--results", default="results")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    out_dir = args.out or os.path.join(args.results, "charts")
    os.makedirs(out_dir, exist_ok=True)

    latency = read_csv(os.path.join(args.results, "latency.csv"))
    topk = read_csv(os.path.join(args.results, "topk.csv"))
    compression = read_csv(os.path.join(args.results, "compression.csv"))
    build = read_csv(os.path.join(args.results, "build.csv"))

    if not any((latency, topk, compression, build)):
        sys.exit("no CSV files found in %s; run the benchmarks first" % args.results)

    latency_by_algorithm(latency, out_dir)
    index_size(compression, out_dir)
    indexing_throughput(build, out_dir)
    series_chart(latency, "dataset_size", "documents in collection (log scale)",
                 "Query latency versus dataset size (k=%s)" % (latency[0]["k"] if latency else ""),
                 "4_latency_vs_dataset_size.png", out_dir, True)
    series_chart(topk, "k", "top-k", "Retrieval latency versus top-k value", "5_latency_vs_topk.png", out_dir, False)


if __name__ == "__main__":
    main()
