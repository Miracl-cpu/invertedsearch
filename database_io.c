#include "database_io.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static void set_path_error(char *error, size_t error_size,
                           const char *action, const char *path)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s '%s': %s",
                       action, path, strerror(errno));
    }
}

static char *copy_string(const char *value)
{
    size_t length = strlen(value) + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL) {
        memcpy(copy, value, length);
    }
    return copy;
}

static int write_escaped_path(FILE *output, const char *path)
{
    static const char HEX[] = "0123456789ABCDEF";
    const unsigned char *cursor = (const unsigned char *)path;

    while (*cursor != '\0') {
        unsigned char value = *cursor++;
        if (value == '%' || value == ':' || value == '#' ||
            value < 32 || value == 127) {
            if (fputc('%', output) == EOF ||
                fputc(HEX[value >> 4], output) == EOF ||
                fputc(HEX[value & 15], output) == EOF) {
                return 0;
            }
        } else if (fputc(value, output) == EOF) {
            return 0;
        }
    }
    return 1;
}

int index_save(const InvertedIndex *index, const char *path,
               char *error, size_t error_size)
{
    size_t path_length;
    char *temporary_path;
    FILE *output;
    size_t bucket;
    int okay = 1;
    int close_okay;

    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    if (index == NULL || path == NULL || path[0] == '\0') {
        set_error(error, error_size, "A non-empty backup path is required.");
        return 0;
    }
    path_length = strlen(path);
    if (path_length > SIZE_MAX - 5) {
        set_error(error, error_size, "Backup path is too long.");
        return 0;
    }
    temporary_path = (char *)malloc(path_length + 5);
    if (temporary_path == NULL) {
        set_error(error, error_size, "Out of memory while saving the index.");
        return 0;
    }
    (void)snprintf(temporary_path, path_length + 5, "%s.tmp", path);
    output = fopen(temporary_path, "wb");
    if (output == NULL) {
        set_path_error(error, error_size, "Cannot create", temporary_path);
        free(temporary_path);
        return 0;
    }

    for (bucket = 0; bucket < INDEX_BUCKET_COUNT && okay; ++bucket) {
        const WordEntry *word;
        if (index->buckets[bucket] == NULL) {
            continue;
        }
        if (fprintf(output, "#:%zu\n", bucket) < 0) {
            okay = 0;
            break;
        }
        for (word = index->buckets[bucket]; word != NULL && okay;
             word = word->next) {
            const FileEntry *file;
            size_t file_count = 0;
            for (file = word->files; file != NULL; file = file->next) {
                ++file_count;
            }
            if (fprintf(output, "%s:%zu", word->word, file_count) < 0) {
                okay = 0;
                break;
            }
            for (file = word->files; file != NULL; file = file->next) {
                if (fputc(':', output) == EOF ||
                    !write_escaped_path(output, file->path) ||
                    fprintf(output, ":%zu", file->count) < 0) {
                    okay = 0;
                    break;
                }
            }
            if (okay && fputs(":#\n", output) == EOF) {
                okay = 0;
            }
        }
    }
    if (okay && fflush(output) != 0) {
        okay = 0;
    }
    close_okay = fclose(output) == 0;
    if (!close_okay) {
        okay = 0;
    }
    if (!okay) {
        set_path_error(error, error_size, "Cannot write", temporary_path);
        (void)remove(temporary_path);
        free(temporary_path);
        return 0;
    }

#ifdef _WIN32
    if (!MoveFileExA(temporary_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size,
                           "Cannot replace backup '%s' (Windows error %lu).",
                           path, (unsigned long)GetLastError());
        }
        (void)remove(temporary_path);
        free(temporary_path);
        return 0;
    }
#else
    if (rename(temporary_path, path) != 0) {
        set_path_error(error, error_size, "Cannot replace", path);
        (void)remove(temporary_path);
        free(temporary_path);
        return 0;
    }
#endif
    free(temporary_path);
    return 1;
}

static char *read_line(FILE *input)
{
    size_t length = 0;
    size_t capacity = 128;
    char *line = (char *)malloc(capacity);
    int character;

    if (line == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    while ((character = fgetc(input)) != EOF) {
        if (character == '\n') {
            break;
        }
        if (length + 1 >= capacity) {
            size_t next_capacity;
            char *grown;
            if (capacity > SIZE_MAX / 2) {
                errno = ENOMEM;
                free(line);
                return NULL;
            }
            next_capacity = capacity * 2;
            grown = (char *)realloc(line, next_capacity);
            if (grown == NULL) {
                errno = ENOMEM;
                free(line);
                return NULL;
            }
            line = grown;
            capacity = next_capacity;
        }
        line[length++] = (char)character;
    }
    if (character == EOF && length == 0) {
        free(line);
        return NULL;
    }
    if (length > 0 && line[length - 1] == '\r') {
        --length;
    }
    line[length] = '\0';
    return line;
}

static int read_unsigned(const char *text, size_t *value)
{
    size_t result = 0;
    const unsigned char *cursor = (const unsigned char *)text;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    while (*cursor != '\0') {
        unsigned int digit;
        if (!isdigit(*cursor)) {
            return 0;
        }
        digit = (unsigned int)(*cursor - '0');
        if (result > (SIZE_MAX - digit) / 10) {
            return 0;
        }
        result = result * 10 + digit;
        ++cursor;
    }
    *value = result;
    return 1;
}

static int hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

static char *decode_path(const char *encoded)
{
    size_t length = strlen(encoded);
    char *decoded = (char *)malloc(length + 1);
    size_t source = 0;
    size_t target = 0;

    if (decoded == NULL) {
        return NULL;
    }
    while (source < length) {
        unsigned char value = (unsigned char)encoded[source++];
        if (value == '%') {
            int high;
            int low;
            if (source + 1 >= length ||
                (high = hex_value(encoded[source])) < 0 ||
                (low = hex_value(encoded[source + 1])) < 0) {
                free(decoded);
                return NULL;
            }
            value = (unsigned char)((high << 4) | low);
            source += 2;
            if (value == '\0') {
                free(decoded);
                return NULL;
            }
        }
        decoded[target++] = (char)value;
    }
    decoded[target] = '\0';
    if (target == 0) {
        free(decoded);
        return NULL;
    }
    return decoded;
}

static int valid_word(const char *word)
{
    const unsigned char *cursor = (const unsigned char *)word;
    if (*cursor == '\0') {
        return 0;
    }
    while (*cursor != '\0') {
        if ((*cursor < 'a' || *cursor > 'z') &&
            (*cursor < 'A' || *cursor > 'Z')) {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

static int add_file_record(InvertedIndex *index, const char *path)
{
    IndexedFile **link = &index->files;
    IndexedFile *record;

    while (*link != NULL && strcmp((*link)->path, path) < 0) {
        link = &(*link)->next;
    }
    if (*link != NULL && strcmp((*link)->path, path) == 0) {
        return 1;
    }
    record = (IndexedFile *)calloc(1, sizeof(*record));
    if (record == NULL) {
        return 0;
    }
    record->path = (char *)malloc(strlen(path) + 1);
    if (record->path == NULL) {
        free(record);
        return 0;
    }
    strcpy(record->path, path);
    record->next = *link;
    *link = record;
    ++index->file_count;
    return 1;
}

static int parse_word_line(InvertedIndex *index, char *line,
                           size_t current_bucket, char **previous_word)
{
    char *cursor = line;
    char *separator;
    char *word;
    size_t file_count;
    size_t i;

    separator = strchr(cursor, ':');
    if (separator == NULL) {
        return 0;
    }
    *separator = '\0';
    word = cursor;
    if (!valid_word(word) || index_bucket_for_word(word) != current_bucket ||
        (*previous_word != NULL && strcmp(*previous_word, word) >= 0)) {
        return 0;
    }
    cursor = separator + 1;
    separator = strchr(cursor, ':');
    if (separator == NULL) {
        return 0;
    }
    *separator = '\0';
    if (!read_unsigned(cursor, &file_count) || file_count == 0) {
        return 0;
    }
    cursor = separator + 1;
    for (i = 0; i < file_count; ++i) {
        char *encoded_path = cursor;
        char *path_separator = strchr(cursor, ':');
        char *count_text;
        char *count_separator;
        char *decoded_path;
        size_t count;
        const WordEntry *existing_word;
        const FileEntry *existing_file;

        if (path_separator == NULL) {
            return 0;
        }
        *path_separator = '\0';
        cursor = path_separator + 1;
        count_text = cursor;
        count_separator = strchr(cursor, ':');
        if (count_separator == NULL) {
            return 0;
        }
        *count_separator = '\0';
        if (!read_unsigned(count_text, &count) || count == 0) {
            return 0;
        }
        cursor = count_separator + 1;
        if (i + 1 == file_count) {
            if (strcmp(cursor, "#") != 0) {
                return 0;
            }
        }
        decoded_path = decode_path(encoded_path);
        if (decoded_path == NULL) {
            return 0;
        }

        existing_word = index_search(index, word);
        if (existing_word != NULL) {
            for (existing_file = existing_word->files;
                 existing_file != NULL; existing_file = existing_file->next) {
                if (strcmp(existing_file->path, decoded_path) == 0) {
                    free(decoded_path);
                    return 0;
                }
            }
        }
        if (!index_import_word_count(index, word, decoded_path, count) ||
            !add_file_record(index, decoded_path)) {
            free(decoded_path);
            return -1;
        }
        free(decoded_path);
    }
    {
        char *copy = copy_string(word);
        if (copy == NULL) {
            return -1;
        }
        free(*previous_word);
        *previous_word = copy;
    }
    return 1;
}

int index_load(InvertedIndex *index, const char *path,
               char *error, size_t error_size)
{
    FILE *input;
    InvertedIndex loaded;
    char *line;
    size_t current_bucket = INDEX_BUCKET_COUNT;
    unsigned char seen_buckets[INDEX_BUCKET_COUNT] = {0};
    char *previous_word = NULL;
    int valid = 1;

    if (index == NULL || path == NULL || path[0] == '\0') {
        set_error(error, error_size, "A non-empty backup path is required.");
        return 0;
    }
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    input = fopen(path, "rb");
    if (input == NULL) {
        set_path_error(error, error_size, "Cannot open", path);
        return 0;
    }
    index_init(&loaded);
    errno = 0;
    while ((line = read_line(input)) != NULL) {
        if (line[0] == '\0') {
            free(line);
            valid = 0;
            break;
        }
        if (line[0] == '#' && line[1] == ':') {
            size_t parsed_bucket;
            if (!read_unsigned(line + 2, &parsed_bucket) ||
                parsed_bucket >= INDEX_BUCKET_COUNT ||
                seen_buckets[parsed_bucket] ||
                (current_bucket != INDEX_BUCKET_COUNT &&
                 parsed_bucket <= current_bucket)) {
                free(line);
                valid = 0;
                break;
            }
            current_bucket = parsed_bucket;
            seen_buckets[parsed_bucket] = 1;
            previous_word = NULL;
            free(line);
            continue;
        }
        if (current_bucket == INDEX_BUCKET_COUNT) {
            free(line);
            valid = 0;
            break;
        }
        {
            int result;
            errno = 0;
            result = parse_word_line(&loaded, line, current_bucket,
                                         &previous_word);
            free(line);
            if (result != 1) {
                valid = 0;
                if (result < 0 || errno == ENOMEM) {
                    set_error(error, error_size,
                              "Out of memory while loading the index.");
                    index_free(&loaded);
                    free(previous_word);
                    (void)fclose(input);
                    return 0;
                }
                break;
            }
        }
        if (errno == ENOMEM) {
            set_error(error, error_size, "Out of memory while reading the backup.");
            valid = 0;
        }
    }
    if (ferror(input)) {
        valid = 0;
    }
    if (fclose(input) != 0) {
        valid = 0;
    }
    if (!valid) {
        if (error == NULL || error_size == 0 || error[0] == '\0') {
            set_error(error, error_size, "Backup is malformed or unreadable.");
        }
        index_free(&loaded);
        free(previous_word);
        return 0;
    }
    free(previous_word);
    index_free(index);
    *index = loaded;
    return 1;
}
