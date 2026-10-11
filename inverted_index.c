#include "inverted_index.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *const STOP_WORDS[] = {
    "a", "an", "and", "are", "as", "at", "be", "been", "but", "by",
    "for", "from", "in", "into", "is", "it", "of", "on", "or", "that",
    "the", "this", "to", "was", "were", "with"
};

static char *copy_string(const char *value)
{
    size_t length = strlen(value) + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL) {
        memcpy(copy, value, length);
    }
    return copy;
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static void set_errno_error(char *error, size_t error_size,
                            const char *action, const char *path)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s '%s': %s",
                       action, path, strerror(errno));
    }
}

void index_init(InvertedIndex *index)
{
    if (index != NULL) {
        memset(index, 0, sizeof(*index));
    }
}

static void free_word_list(WordEntry *word)
{
    while (word != NULL) {
        WordEntry *next_word = word->next;
        FileEntry *file = word->files;
        while (file != NULL) {
            FileEntry *next_file = file->next;
            free(file->path);
            free(file);
            file = next_file;
        }
        free(word->word);
        free(word);
        word = next_word;
    }
}

void index_free(InvertedIndex *index)
{
    size_t bucket;
    IndexedFile *file;

    if (index == NULL) {
        return;
    }
    for (bucket = 0; bucket < INDEX_BUCKET_COUNT; ++bucket) {
        free_word_list(index->buckets[bucket]);
    }
    file = index->files;
    while (file != NULL) {
        IndexedFile *next = file->next;
        free(file->path);
        free(file);
        file = next;
    }
    index_init(index);
}

size_t index_bucket_for_word(const char *word)
{
    unsigned char first;

    if (word == NULL || word[0] == '\0') {
        return INDEX_BUCKET_COUNT - 1;
    }
    first = (unsigned char)tolower((unsigned char)word[0]);
    if (first >= 'a' && first <= 'z') {
        return (size_t)(first - 'a');
    }
    return INDEX_BUCKET_COUNT - 1;
}

static int is_stop_word(const char *word)
{
    size_t i;
    const size_t count = sizeof(STOP_WORDS) / sizeof(STOP_WORDS[0]);

    for (i = 0; i < count; ++i) {
        const unsigned char *word_cursor = (const unsigned char *)word;
        const unsigned char *stop_cursor =
            (const unsigned char *)STOP_WORDS[i];
        while (*word_cursor != '\0' && *stop_cursor != '\0' &&
               tolower(*word_cursor) == *stop_cursor) {
            ++word_cursor;
            ++stop_cursor;
        }
        if (*word_cursor == '\0' && *stop_cursor == '\0') {
            return 1;
        }
    }
    return 0;
}

static WordEntry *find_word(InvertedIndex *index, const char *word,
                            WordEntry ***link_out)
{
    size_t bucket = index_bucket_for_word(word);
    WordEntry **link = &index->buckets[bucket];

    while (*link != NULL && strcmp((*link)->word, word) < 0) {
        link = &(*link)->next;
    }
    if (link_out != NULL) {
        *link_out = link;
    }
    return *link != NULL && strcmp((*link)->word, word) == 0 ? *link : NULL;
}

static int add_count(InvertedIndex *index, const char *word,
                     const char *path, size_t count)
{
    WordEntry **word_link;
    WordEntry *entry = find_word(index, word, &word_link);
    FileEntry **file_link;

    if (entry == NULL) {
        entry = (WordEntry *)calloc(1, sizeof(*entry));
        if (entry == NULL) {
            return 0;
        }
        entry->word = copy_string(word);
        if (entry->word == NULL) {
            free(entry);
            return 0;
        }
        entry->next = *word_link;
        *word_link = entry;
        ++index->word_count;
    }

    file_link = &entry->files;
    while (*file_link != NULL && strcmp((*file_link)->path, path) < 0) {
        file_link = &(*file_link)->next;
    }
    if (*file_link != NULL && strcmp((*file_link)->path, path) == 0) {
        if (SIZE_MAX - (*file_link)->count < count) {
            return 0;
        }
        (*file_link)->count += count;
        return 1;
    }

    {
        FileEntry *file = (FileEntry *)calloc(1, sizeof(*file));
        if (file == NULL) {
            return 0;
        }
        file->path = copy_string(path);
        if (file->path == NULL) {
            free(file);
            return 0;
        }
        file->count = count;
        file->next = *file_link;
        *file_link = file;
    }
    return 1;
}

int index_import_word_count(InvertedIndex *index, const char *word,
                            const char *path, size_t count)
{
    return index != NULL && word != NULL && path != NULL && count > 0
        ? add_count(index, word, path, count)
        : 0;
}

static int flush_token(InvertedIndex *staged, const char *token)
{
    if (token[0] == '\0' || is_stop_word(token)) {
        return 1;
    }
    return add_count(staged, token, "", 1);
}

static void rollback_path(InvertedIndex *index, const char *path)
{
    size_t bucket;

    for (bucket = 0; bucket < INDEX_BUCKET_COUNT; ++bucket) {
        WordEntry **word_link = &index->buckets[bucket];
        while (*word_link != NULL) {
            WordEntry *word = *word_link;
            FileEntry **file_link = &word->files;
            while (*file_link != NULL) {
                FileEntry *file = *file_link;
                if (strcmp(file->path, path) == 0) {
                    *file_link = file->next;
                    free(file->path);
                    free(file);
                    break;
                }
                file_link = &file->next;
            }
            if (word->files == NULL) {
                *word_link = word->next;
                free(word->word);
                free(word);
                --index->word_count;
            } else {
                word_link = &word->next;
            }
        }
    }
}

static int file_is_indexed(const InvertedIndex *index, const char *path)
{
    const IndexedFile *file;
    for (file = index->files; file != NULL; file = file->next) {
        if (strcmp(file->path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int add_indexed_file_record(InvertedIndex *index, IndexedFile *record)
{
    IndexedFile **link = &index->files;
    while (*link != NULL && strcmp((*link)->path, record->path) < 0) {
        link = &(*link)->next;
    }
    record->next = *link;
    *link = record;
    ++index->file_count;
    return 1;
}

int index_add_file(InvertedIndex *index, const char *path,
                   char *error, size_t error_size)
{
    FILE *input;
    InvertedIndex staged;
    IndexedFile *record;
    size_t token_length = 0;
    size_t token_capacity = 32;
    char *token;
    int character;
    int success = 0;
    size_t bucket;

    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    if (index == NULL || path == NULL || path[0] == '\0') {
        set_error(error, error_size, "A non-empty file path is required.");
        return 0;
    }
    if (file_is_indexed(index, path)) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size,
                           "File '%s' is already indexed.", path);
        }
        return 0;
    }
    input = fopen(path, "rb");
    if (input == NULL) {
        set_errno_error(error, error_size, "Cannot open", path);
        return 0;
    }

    token = (char *)malloc(token_capacity);
    record = (IndexedFile *)calloc(1, sizeof(*record));
    if (token == NULL || record == NULL) {
        set_error(error, error_size, "Out of memory while indexing the file.");
        goto cleanup;
    }
    record->path = copy_string(path);
    if (record->path == NULL) {
        set_error(error, error_size, "Out of memory while indexing the file.");
        goto cleanup;
    }
    index_init(&staged);

    while ((character = fgetc(input)) != EOF) {
        unsigned char value = (unsigned char)character;
        if (isalpha(value)) {
            if (token_length + 1 >= token_capacity) {
                size_t new_capacity;
                char *grown;
                if (token_capacity > SIZE_MAX / 2) {
                    set_error(error, error_size, "A word is too large to index.");
                    goto cleanup_staged;
                }
                new_capacity = token_capacity * 2;
                grown = (char *)realloc(token, new_capacity);
                if (grown == NULL) {
                    set_error(error, error_size,
                              "Out of memory while reading a word.");
                    goto cleanup_staged;
                }
                token = grown;
                token_capacity = new_capacity;
            }
            token[token_length++] = (char)value;
        } else if (token_length > 0) {
            token[token_length] = '\0';
            if (!flush_token(&staged, token)) {
                set_error(error, error_size,
                          "Out of memory while building the file index.");
                goto cleanup_staged;
            }
            token_length = 0;
        }
    }
    if (ferror(input)) {
        set_errno_error(error, error_size, "Cannot read", path);
        goto cleanup_staged;
    }
    if (token_length > 0) {
        token[token_length] = '\0';
        if (!flush_token(&staged, token)) {
            set_error(error, error_size,
                      "Out of memory while building the file index.");
            goto cleanup_staged;
        }
    }
    if (fclose(input) != 0) {
        input = NULL;
        set_errno_error(error, error_size, "Cannot close", path);
        goto cleanup_staged;
    }
    input = NULL;

    for (bucket = 0; bucket < INDEX_BUCKET_COUNT; ++bucket) {
        WordEntry *word;
        for (word = staged.buckets[bucket]; word != NULL; word = word->next) {
            if (!add_count(index, word->word, path, word->files->count)) {
                set_error(error, error_size,
                          "Out of memory while merging the file index.");
                rollback_path(index, path);
                goto cleanup_staged;
            }
        }
    }
    if (staged.word_count > 0) {
        add_indexed_file_record(index, record);
        record = NULL;
    }
    success = 1;

cleanup_staged:
    index_free(&staged);
cleanup:
    if (input != NULL) {
        (void)fclose(input);
    }
    free(token);
    if (record != NULL) {
        free(record->path);
        free(record);
    }
    return success;
}

const WordEntry *index_search(const InvertedIndex *index, const char *word)
{
    size_t bucket;
    const WordEntry *entry;
    const unsigned char *cursor;
    unsigned char first;

    if (index == NULL || word == NULL || word[0] == '\0') {
        return NULL;
    }
    first = (unsigned char)word[0];
    first = (unsigned char)tolower(first);
    if (first < 'a' || first > 'z') {
        return NULL;
    }
    for (cursor = (const unsigned char *)word; *cursor != '\0'; ++cursor) {
        if (!isalpha(*cursor)) {
            return NULL;
        }
    }
    bucket = (size_t)(first - 'a');
    entry = index->buckets[bucket];
    while (entry != NULL) {
        int comparison = strcmp(word, entry->word);
        if (comparison <= 0) {
            return comparison == 0 ? entry : NULL;
        }
        entry = entry->next;
    }
    return NULL;
}

void index_display(const InvertedIndex *index, FILE *output)
{
    size_t bucket;
    size_t total_occurrences = 0;

    if (index == NULL || output == NULL) {
        return;
    }
    fprintf(output, "Indexed files: %zu | Unique words: %zu\n",
            index->file_count, index->word_count);
    for (bucket = 0; bucket < INDEX_BUCKET_COUNT; ++bucket) {
        const WordEntry *word;
        if (index->buckets[bucket] == NULL) {
            continue;
        }
        fprintf(output, "\nBucket %zu:\n", bucket);
        for (word = index->buckets[bucket]; word != NULL; word = word->next) {
            const FileEntry *file;
            fprintf(output, "  %-20s", word->word);
            for (file = word->files; file != NULL; file = file->next) {
                fprintf(output, " %s(%zu)", file->path, file->count);
                total_occurrences += file->count;
            }
            fputc('\n', output);
        }
    }
    fprintf(output, "\nTotal indexed occurrences: %zu\n", total_occurrences);
}
