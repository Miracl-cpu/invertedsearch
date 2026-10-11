# Inverted Search

A portable C command-line program that builds an in-memory inverted index for
text files. Instead of scanning every source file for each query, it looks up a
word in one of 64 hash buckets and reports the files and occurrence counts
already associated with that word.

## Build

### Windows (MinGW)

Install MinGW GCC and GNU Make, then run in this folder:

```powershell
mingw32-make
mingw32-make test
```

The executable is `Slist.exe`.

### Linux or macOS

Install a C11 compiler and Make, then run:

```sh
make
make test
```

The executable is `Slist`.

## Try it

Build and index both included sample documents:

```text
Slist.exe --index file1.txt --add file2.txt --show --search Hi --save index.db
```

On Linux or macOS, replace `Slist.exe` with `./Slist`. Searches are case-sensitive: `Hi`, `hi`, and `HI` are separate indexed words,
although words beginning with the same letter (regardless of case) share a
bucket. Search results show a separate occurrence count for each matching
file. The same commands can be
entered through the interactive menu by running the executable without
arguments.

Demonstrate persistence and incremental updates in a second run:

```text
Slist.exe --load index.db --add another.txt --search Hi --save index.db
```

`--index` starts a fresh in-memory index; `--add` appends files to the current
one. An exact path cannot be indexed twice in one index, preventing accidental
double counting. A file with no searchable words is read successfully but is
not included in the indexed-file count.

## Design

- **Hash table:** 64 bucket heads; the first letter (case-insensitive) maps
  `a` through `z` to buckets `0` through `25` (`h`/`H` maps to `7`). Bucket
  `63` is the fallback for special initials at the hash-function boundary;
  punctuation in source text is a delimiter, not an indexed word.
- **Collision handling:** each bucket is a singly linked list of word nodes,
  maintained in case-sensitive alphabetical order.
- **Posting lists:** each word owns a second linked list of file paths and
  positive occurrence counts; results are kept in path order.
- **Parsing:** files are read one character at a time. Any non-alphabetic
  character delimits a word; original letter case is preserved. A short,
  documented stop-word list (common articles, conjunctions, and prepositions)
  is skipped without regard to case.
- **Memory ownership:** the index owns its word, path, posting-list, and file
  record allocations. `index_free` releases the complete structure.
- **Failure behavior:** a file is first parsed into a temporary index and then
  merged. Read errors, duplicate paths, and allocation errors are reported;
  a failed merge rolls back that file's postings.
- **Persistence:** backups use the requested `#:<bucket>` sections and
  `word:file_count:path:count...:#` records. Reserved path bytes (`%`, `:`,
  `#`, and control characters) are percent-escaped. Loading validates the
  entire backup into a temporary index and replaces the active index only
  after successful validation. Saving writes a temporary file and replaces
  the destination after a successful flush.

Only files containing at least one non-stop word are serialized, so blank or
stop-word-only files are not counted as indexed files.

## Complexity

For a word with `d` characters and `p` posting-list entries, lookup is
`O(d + b + p)`, where `b` is the number of words in its bucket. Adding a file
is linear in its input size plus the ordered-list insertion costs. This
implementation favors an understandable linked-list design; it does not claim
constant-time lookup within a bucket or benchmark-specific performance.

## Suggested live demo

1. Run `Slist.exe --index file1.txt --show` to display bucket placement and
   counts.
2. Run `Slist.exe --load index.db --add file2.txt --search Hi --show` to show
   incremental indexing and per-file frequency results.
3. Save and reload the backup, then repeat the search to demonstrate that the
   source files do not need to be re-parsed.
4. Explain the ingestion-versus-query trade-off, linked-list memory ownership,
   malformed-backup handling, and stop-word behavior.

## Project files

- `main.c`: interactive menu and command-line interface
- `inverted_index.c` / `inverted_index.h`: parsing, indexing, lookup, display,
  and memory cleanup
- `database_io.c` / `database_io.h`: validated save/load support
- `tests/test_index.c`: parser, update, lookup, persistence, and failure tests
- `Makefile`: build, test, and clean targets
