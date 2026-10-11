#ifndef DATABASE_IO_H
#define DATABASE_IO_H

#include <stddef.h>

#include "inverted_index.h"

int index_save(const InvertedIndex *index, const char *path,
               char *error, size_t error_size);
int index_load(InvertedIndex *index, const char *path,
               char *error, size_t error_size);

#endif
