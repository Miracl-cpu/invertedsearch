#ifndef INVERTED_INDEX_H
#define INVERTED_INDEX_H

#include <stddef.h>
#include <stdio.h>

#define INDEX_BUCKET_COUNT 64

typedef struct FileEntry {
    char *path;
    size_t count;
    struct FileEntry *next;
} FileEntry;

typedef struct WordEntry {
    char *word;
    FileEntry *files;
    struct WordEntry *next;
} WordEntry;

typedef struct IndexedFile {
    char *path;
    struct IndexedFile *next;
} IndexedFile;

typedef struct {
    WordEntry *buckets[INDEX_BUCKET_COUNT];
    IndexedFile *files;
    size_t word_count;
    size_t file_count;
} InvertedIndex;

/* Initialize an index before passing it to any other API function. */
void index_init(InvertedIndex *index);
void index_free(InvertedIndex *index);

/* Add a file once. Re-adding an indexed path is an error, not a double count. */
int index_add_file(InvertedIndex *index, const char *path,
                   char *error, size_t error_size);

/* Search is case-sensitive and returns NULL for an invalid or absent word. */
const WordEntry *index_search(const InvertedIndex *index, const char *word);
void index_display(const InvertedIndex *index, FILE *output);

size_t index_bucket_for_word(const char *word);

/* Internal deserialization helper for a word/path pair and positive count. */
int index_import_word_count(InvertedIndex *index, const char *word,
                            const char *path, size_t count);

#endif
