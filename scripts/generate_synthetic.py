import argparse
import bisect
import itertools
import os
import random


def build_cdf(vocabulary, exponent):
    weights = [1.0 / ((rank + 1) ** exponent) for rank in range(vocabulary)]
    return list(itertools.accumulate(weights))


def draw(rng, cdf):
    return bisect.bisect_left(cdf, rng.random() * cdf[-1])


def main():
    parser = argparse.ArgumentParser(description="Generate a synthetic Zipfian corpus and query set")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--docs", type=int, default=100000)
    parser.add_argument("--queries", type=int, default=1000)
    parser.add_argument("--vocabulary", type=int, default=50000)
    parser.add_argument("--mean-length", type=int, default=60)
    parser.add_argument("--seed", type=int, default=13)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    cdf = build_cdf(args.vocabulary, 1.05)
    query_cdf = build_cdf(args.vocabulary, 0.85)

    os.makedirs(args.out_dir, exist_ok=True)
    with open(os.path.join(args.out_dir, "documents.tsv"), "w", encoding="utf-8") as out:
        for doc_id in range(args.docs):
            length = max(3, int(rng.gauss(args.mean_length, args.mean_length / 3)))
            words = ["t%d" % draw(rng, cdf) for _ in range(length)]
            out.write("%d\t%s\n" % (doc_id, " ".join(words)))

    with open(os.path.join(args.out_dir, "queries.tsv"), "w", encoding="utf-8") as out:
        for query_id in range(args.queries):
            terms = rng.randint(2, 6)
            words = ["t%d" % draw(rng, query_cdf) for _ in range(terms)]
            out.write("%d\t%s\n" % (query_id, " ".join(words)))

    print("wrote %d documents and %d queries to %s" % (args.docs, args.queries, args.out_dir))


if __name__ == "__main__":
    main()
