#!/bin/sh
set -eu

BIN="$1"
DATA="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

"$BIN" index --input "$DATA/documents.tsv" --output "$WORK/c.bin" --threads 3
"$BIN" index --input "$DATA/documents.tsv" --output "$WORK/u.bin" --threads 1 --compression none
"$BIN" info --index "$WORK/c.bin" | grep -q "compressed"
"$BIN" info --index "$WORK/u.bin" | grep -q "uncompressed"

"$BIN" search --index "$WORK/c.bin" --query "efficient machine learning" --topk 3 | grep -q "Doc 10"
"$BIN" search --index "$WORK/u.bin" --query "tomato soup" --algorithm exhaustive | grep -q "Doc 3"
"$BIN" search --index "$WORK/c.bin" --query "zzzunknownterm" | grep -q "No matching documents"

printf 'efficient machine learning\n:quit\n' | "$BIN" interactive --index "$WORK/c.bin" | grep -q "Doc 10"

"$BIN" benchmark --index "$WORK/c.bin" --queries "$DATA/queries.tsv" --output "$WORK/bench.csv" --warmup 2 > /dev/null
test "$(wc -l < "$WORK/bench.csv")" -eq 3

"$BIN" evaluate --index "$WORK/c.bin" --queries "$DATA/queries.tsv" --qrels "$DATA/qrels.tsv" | grep -q "MRR@10"

if "$BIN" search --index "$WORK/c.bin" > /dev/null 2>&1; then exit 1; fi
if "$BIN" search --index "$WORK/missing.bin" --query x > /dev/null 2>&1; then exit 1; fi
if "$BIN" index --input "$WORK/missing.tsv" --output "$WORK/o.bin" > /dev/null 2>&1; then exit 1; fi
if "$BIN" search --index "$WORK/c.bin" --query x --topk abc > /dev/null 2>&1; then exit 1; fi
if "$BIN" bogus > /dev/null 2>&1; then exit 1; fi

printf 'garbage' > "$WORK/bad.bin"
if "$BIN" search --index "$WORK/bad.bin" --query x > /dev/null 2>&1; then exit 1; fi

printf 'only one field\n' > "$WORK/bad.tsv"
if "$BIN" index --input "$WORK/bad.tsv" --output "$WORK/o.bin" > /dev/null 2>&1; then exit 1; fi
