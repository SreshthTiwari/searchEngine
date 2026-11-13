import argparse
import os
import random
import sys


def read_qrels(path):
    relevant = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            fields = line.split()
            if len(fields) < 4:
                continue
            qid, _, pid, rel = fields[:4]
            if int(rel) > 0:
                relevant.setdefault(qid, set()).add(pid)
    return relevant


def read_queries(path):
    queries = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line:
                continue
            qid, _, text = line.partition("\t")
            queries[qid] = text
    return queries


def count_lines(path):
    total = 0
    with open(path, "rb") as handle:
        for _ in handle:
            total += 1
    return total


def main():
    parser = argparse.ArgumentParser(description="Build an MS MARCO passage subset for the search engine")
    parser.add_argument("--collection", required=True, help="collection.tsv (pid<TAB>passage)")
    parser.add_argument("--queries", required=True, help="queries.dev.small.tsv (qid<TAB>query)")
    parser.add_argument("--qrels", required=True, help="qrels.dev.small.tsv (qid 0 pid rel)")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--size", type=int, default=100000, help="number of passages in the subset")
    parser.add_argument("--seed", type=int, default=13)
    args = parser.parse_args()

    random.seed(args.seed)
    relevant = read_qrels(args.qrels)
    queries = read_queries(args.queries)
    judged = {qid: text for qid, text in queries.items() if qid in relevant}
    must_keep = set()
    for qid in judged:
        must_keep |= relevant[qid]

    if args.size < len(must_keep):
        sys.exit("--size must be at least %d to keep every relevant passage" % len(must_keep))

    total = count_lines(args.collection)
    filler_needed = args.size - len(must_keep)

    chosen_lines = set(random.sample(range(total), min(total, filler_needed + len(must_keep) * 2)))

    selected = []
    filler = 0
    with open(args.collection, encoding="utf-8") as handle:
        for line_number, line in enumerate(handle):
            line = line.rstrip("\n")
            pid, _, text = line.partition("\t")
            if pid in must_keep:
                selected.append((pid, text))
            elif line_number in chosen_lines and filler < filler_needed:
                selected.append((pid, text))
                filler += 1

    random.shuffle(selected)

    os.makedirs(args.out_dir, exist_ok=True)
    with open(os.path.join(args.out_dir, "documents.tsv"), "w", encoding="utf-8") as out:
        for pid, text in selected:
            out.write("%s\t%s\n" % (pid, text.replace("\t", " ")))
    with open(os.path.join(args.out_dir, "queries.tsv"), "w", encoding="utf-8") as out:
        for qid, text in judged.items():
            out.write("%s\t%s\n" % (qid, text.replace("\t", " ")))
    with open(os.path.join(args.out_dir, "qrels.tsv"), "w", encoding="utf-8") as out:
        for qid in judged:
            for pid in sorted(relevant[qid]):
                out.write("%s\t0\t%s\t1\n" % (qid, pid))

    print("passages written: %d (relevant kept: %d, filler: %d)" % (len(selected), len(must_keep), filler))
    print("queries with judgements: %d" % len(judged))


if __name__ == "__main__":
    main()
