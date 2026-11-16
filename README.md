# C++ Search Engine

A full-text search engine written from scratch in C++20. It builds an inverted index over a document collection, ranks results with BM25, compresses posting lists with delta and variable-byte encoding, speeds up top-k retrieval with WAND, indexes in parallel, and saves indexes to a versioned binary format. It ships with a command-line interface and a simple web page, and was benchmarked on up to one million MS MARCO passages.

Build, usage, design notes, benchmark methodology and results are in [description.md](description.md).
