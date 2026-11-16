# Data

Nothing in this directory is committed except this file and `sample/`, a tiny hand-written collection used by the smoke test and the quick-start in `description.md`.

## File formats

**Documents** (`--input`), either:

* TSV, one document per line: `id<TAB>text` or `id<TAB>title<TAB>text`. This is the layout of the MS MARCO `collection.tsv`.
* JSONL, one object per line: `{"id": "...", "title": "...", "text": "..."}`. `id` and `text` are required, `title` is optional, other fields are ignored.

The format is picked from the extension (`.jsonl` / `.json` mean JSONL, anything else TSV) or with `--format`.

**Queries** (`--queries`): `qid<TAB>query` per line, or just the query text.

**Judgements** (`qrels`): TREC layout `qid 0 docid relevance`, whitespace separated. Only rows with relevance > 0 count as relevant.

## MS MARCO passage ranking

```bash
mkdir -p data/raw && cd data/raw
curl -LO https://msmarco.z22.web.core.windows.net/msmarcoranking/collectionandqueries.tar.gz
tar -xzf collectionandqueries.tar.gz collection.tsv queries.dev.small.tsv qrels.dev.small.tsv
cd ../..

python3 scripts/prepare_msmarco.py \
  --collection data/raw/collection.tsv \
  --queries data/raw/queries.dev.small.tsv \
  --qrels data/raw/qrels.dev.small.tsv \
  --out-dir data/msmarco_100k --size 100000
```

`prepare_msmarco.py` keeps every passage that is judged relevant for a dev-small query, fills the remaining slots with a seeded random sample of the other passages, shuffles the result, and writes `documents.tsv`, `queries.tsv` and `qrels.tsv`. Use `--size 1000000` for the one-million-passage collection.

Because the subset contains far fewer distractor passages than the full 8.8M-passage collection, MRR@10 on a subset is much higher than the official leaderboard number and must not be compared against it.

## Synthetic data

For quick experiments without a download:

```bash
python3 scripts/generate_synthetic.py --out-dir data/synthetic --docs 100000 --queries 1000
```

This produces Zipf-distributed tokens, so posting-list length distributions look natural, but there is no relevance structure and no qrels.
