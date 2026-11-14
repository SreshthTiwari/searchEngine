#!/bin/sh
set -eu

DOCS="${1:?usage: run_benchmarks.sh DOCUMENTS_TSV QUERIES_TSV [RESULTS_DIR] [SIZES]}"
QUERIES="${2:?usage: run_benchmarks.sh DOCUMENTS_TSV QUERIES_TSV [RESULTS_DIR] [SIZES]}"
RESULTS="${3:-results}"
SIZES="${4:-}"
BUILD_DIR="${BUILD_DIR:-build}"
RUNS="${RUNS:-3}"
THREADS="${THREADS:-1 2 4 8}"
MAX_QUERIES="${MAX_QUERIES:-0}"
REPEATS="${REPEATS:-3}"
LABEL="${LABEL:-$(basename "$(dirname "$DOCS")")}"

mkdir -p "$RESULTS"
rm -f "$RESULTS/build.csv"

for t in $THREADS; do
  run=1
  while [ "$run" -le "$RUNS" ]; do
    "$BUILD_DIR/benchmark_index" build --input "$DOCS" --threads "$t" --run "$run" --label "$LABEL" --output "$RESULTS/build.csv"
    run=$((run + 1))
  done
done

if [ -n "$SIZES" ]; then
  "$BUILD_DIR/benchmark_index" compression --input "$DOCS" --sizes "$SIZES" --label "$LABEL" --output "$RESULTS/compression.csv"
  "$BUILD_DIR/benchmark_search" --input "$DOCS" --queries "$QUERIES" --sizes "$SIZES" --label "$LABEL" --max-queries "$MAX_QUERIES" --repeats "$REPEATS" --output-dir "$RESULTS"
else
  "$BUILD_DIR/benchmark_index" compression --input "$DOCS" --label "$LABEL" --output "$RESULTS/compression.csv"
  "$BUILD_DIR/benchmark_search" --input "$DOCS" --queries "$QUERIES" --label "$LABEL" --max-queries "$MAX_QUERIES" --repeats "$REPEATS" --output-dir "$RESULTS"
fi

python3 benchmarks/plot_results.py --results "$RESULTS"
