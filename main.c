#include "database_io.h"
#include "inverted_index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_BUFFER_SIZE 4096
#define ERROR_BUFFER_SIZE 512

static void print_usage(const char *program)
{
    printf("Inverted Search - C text-file index\n\n");
    printf("Usage:\n");
    printf("  %s                         Open the interactive menu\n", program);
    printf("  %s --index FILE...         Build an index from files\n", program);
    printf("  %s --load BACKUP           Load a saved index\n", program);
    printf("  %s --add FILE...           Add files to the current index\n", program);
    printf("  %s --search WORD           Search for a word\n", program);
    printf("  %s --show                  Display the complete index\n", program);
    printf("  %s --save BACKUP           Save the current index\n\n", program);
    printf("Options can be combined in order, for example:\n");
    printf("  %s --index file1.txt --add file2.txt --search Hi --save index.db\n",
           program);
}

static int read_input(const char *prompt, char *buffer, size_t capacity)
{
    size_t length;
    int character;

    fputs(prompt, stdout);
    fflush(stdout);
    if (fgets(buffer, (int)capacity, stdin) == NULL) {
        return 0;
    }
    length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n') {
        buffer[--length] = '\0';
    } else if (!feof(stdin)) {
        while ((character = getchar()) != '\n' && character != EOF) {
        }
        fprintf(stderr, "Input is too long (maximum %zu characters).\n",
                capacity - 1);
        return -1;
    }
    if (length > 0 && buffer[length - 1] == '\r') {
        buffer[length - 1] = '\0';
    }
    return 1;
}

static void print_search_result(const InvertedIndex *index, const char *word)
{
    const WordEntry *entry = index_search(index, word);
    const FileEntry *file;

    if (entry == NULL) {
        printf("No indexed matches for '%s'.\n", word);
        return;
    }
    printf("Matches for '%s':\n", entry->word);
    for (file = entry->files; file != NULL; file = file->next) {
        printf("  %-32s %zu occurrence%s\n", file->path, file->count,
               file->count == 1 ? "" : "s");
    }
}

static int add_one_file(InvertedIndex *index, const char *path)
{
    char error[ERROR_BUFFER_SIZE] = "";
    size_t previous_file_count = index->file_count;

    if (!index_add_file(index, path, error, sizeof(error))) {
        fprintf(stderr, "Error: %s\n", error);
        return 0;
    }
    if (index->file_count == previous_file_count) {
        printf("Read '%s'; it contains no searchable words.\n", path);
    } else {
        printf("Indexed '%s'.\n", path);
    }
    return 1;
}

static int run_menu(InvertedIndex *index)
{
    char input[INPUT_BUFFER_SIZE];
    char error[ERROR_BUFFER_SIZE];

    for (;;) {
        int result;
        int choice;
        printf("\nInverted Search\n");
        printf("1. Add/index a text file\n");
        printf("2. Display the index\n");
        printf("3. Search for a word\n");
        printf("4. Save the index\n");
        printf("5. Load an index\n");
        printf("6. Exit\n");
        result = read_input("Choice: ", input, sizeof(input));
        if (result == 0) {
            putchar('\n');
            return 0;
        }
        if (result < 0 || sscanf(input, "%d", &choice) != 1) {
            puts("Enter a number from 1 to 6.");
            continue;
        }
        if (choice == 1) {
            result = read_input("Text file path: ", input, sizeof(input));
            if (result > 0 && input[0] != '\0') {
                (void)add_one_file(index, input);
            } else if (result == 0) {
                putchar('\n');
                return 0;
            }
        } else if (choice == 2) {
            index_display(index, stdout);
        } else if (choice == 3) {
            result = read_input("Word to search: ", input, sizeof(input));
            if (result > 0 && input[0] != '\0') {
                print_search_result(index, input);
            } else if (result == 0) {
                putchar('\n');
                return 0;
            }
        } else if (choice == 4) {
            result = read_input("Backup path: ", input, sizeof(input));
            if (result > 0 && input[0] != '\0') {
                error[0] = '\0';
                if (index_save(index, input, error, sizeof(error))) {
                    printf("Saved index to '%s'.\n", input);
                } else {
                    fprintf(stderr, "Error: %s\n", error);
                }
            } else if (result == 0) {
                putchar('\n');
                return 0;
            }
        } else if (choice == 5) {
            result = read_input("Backup path: ", input, sizeof(input));
            if (result > 0 && input[0] != '\0') {
                error[0] = '\0';
                if (index_load(index, input, error, sizeof(error))) {
                    printf("Loaded index from '%s'.\n", input);
                } else {
                    fprintf(stderr, "Error: %s\n", error);
                }
            } else if (result == 0) {
                putchar('\n');
                return 0;
            }
        } else if (choice == 6) {
            return 0;
        } else {
            puts("Enter a number from 1 to 6.");
        }
    }
}

static int run_arguments(InvertedIndex *index, int argc, char **argv)
{
    int argument = 1;
    int failed = 0;

    while (argument < argc) {
        const char *option = argv[argument++];
        if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(option, "--show") == 0) {
            index_display(index, stdout);
        } else if (strcmp(option, "--index") == 0 ||
                   strcmp(option, "--add") == 0) {
            int reset = strcmp(option, "--index") == 0;
            int added = 0;
            if (reset) {
                index_free(index);
            }
            while (argument < argc && strncmp(argv[argument], "--", 2) != 0) {
                if (!add_one_file(index, argv[argument])) {
                    failed = 1;
                }
                ++argument;
                ++added;
            }
            if (added == 0) {
                fprintf(stderr, "Error: %s requires at least one file path.\n",
                        option);
                return 1;
            }
        } else if (strcmp(option, "--load") == 0 ||
                   strcmp(option, "--save") == 0) {
            char error[ERROR_BUFFER_SIZE] = "";
            int is_load = strcmp(option, "--load") == 0;
            int okay;
            if (argument >= argc ||
                strncmp(argv[argument], "--", 2) == 0) {
                fprintf(stderr, "Error: %s requires a backup path.\n", option);
                return 1;
            }
            okay = is_load
                ? index_load(index, argv[argument], error, sizeof(error))
                : index_save(index, argv[argument], error, sizeof(error));
            if (!okay) {
                fprintf(stderr, "Error: %s\n", error);
                failed = 1;
            } else {
                printf("%s index '%s'.\n",
                       is_load ? "Loaded" : "Saved", argv[argument]);
            }
            ++argument;
        } else if (strcmp(option, "--search") == 0) {
            if (argument >= argc ||
                strncmp(argv[argument], "--", 2) == 0) {
                fprintf(stderr, "Error: --search requires a word.\n");
                return 1;
            }
            print_search_result(index, argv[argument++]);
        } else {
            fprintf(stderr, "Error: unknown option '%s'.\n", option);
            print_usage(argv[0]);
            return 1;
        }
    }
    return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
    InvertedIndex index;
    int result;

    index_init(&index);
    result = argc == 1 ? run_menu(&index) : run_arguments(&index, argc, argv);
    index_free(&index);
    return result;
}
