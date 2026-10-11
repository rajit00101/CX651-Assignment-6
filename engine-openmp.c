#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <omp.h>
#include <ctype.h>
#include <sys/types.h>

#define MAX_LINE_LENGTH 256
#define THREAD_COUNT 4

struct worker_args {
    char *filename;
    char *target;
    off_t start;
    off_t end;
    bool collect_instances;
    bool failed;
    struct count_result result;
};

static int matches_in_line(const char *line, const char *target) {
    int count = 0;
    size_t target_length = strlen(target);
    const char *match = line;

    while ((match = strstr(match, target)) != NULL) {
        count++;
        match += target_length;
    }

    return count;
}

static char *copy_trimmed_line(const char *line) {
    const char *start = line;
    const char *end = line + strlen(line);

    while (start < end && isspace((unsigned char)*start)) {
        start++;
    }
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }

    size_t length = (size_t)(end - start);
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

static void clear_result(struct count_result *result) {
    for (int i = 0; i < result->count; i++) {
        free(result->instances[i]);
    }
    free(result->instances);
    result->instances = NULL;
    result->count = 0;
}

static bool append_instances(struct count_result *result, size_t *capacity,
                             const char *line, int matches) {
    size_t needed = (size_t)result->count + (size_t)matches;
    if (needed > *capacity) {
        size_t new_capacity = *capacity == 0 ? 16 : *capacity;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }

        char **resized = realloc(result->instances,
                                 new_capacity * sizeof(*resized));
        if (resized == NULL) {
            return false;
        }
        result->instances = resized;
        *capacity = new_capacity;
    }

    int first_new = result->count;
    for (int i = 0; i < matches; i++) {
        result->instances[result->count] = copy_trimmed_line(line);
        if (result->instances[result->count] == NULL) {
            for (int j = first_new; j < result->count; j++) {
                free(result->instances[j]);
            }
            result->count = first_new;
            return false;
        }
        result->count++;
    }

    return true;
}

static void search_chunk(struct worker_args *args) {
    FILE *file = fopen(args->filename, "r");
    if (file == NULL) {
        args->failed = true;
        return;
    }

    size_t line_capacity = MAX_LINE_LENGTH;
    char *line = malloc(line_capacity);
    if (line == NULL) {
        args->failed = true;
        fclose(file);
        return;
    }

    off_t position = args->start;
    if (args->start > 0) {
        if (fseeko(file, args->start - 1, SEEK_SET) != 0) {
            args->failed = true;
        } else if (fgetc(file) != '\n') {
            ssize_t skipped = getline(&line, &line_capacity, file);
            if (skipped < 0) {
                position = args->end;
            } else {
                position += skipped;
            }
        }
    } else if (fseeko(file, 0, SEEK_SET) != 0) {
        args->failed = true;
    }

    size_t instance_capacity = 0;
    while (!args->failed && position < args->end) {
        ssize_t length = getline(&line, &line_capacity, file);
        if (length < 0) {
            break;
        }
        position += length;

        int matches = matches_in_line(line, args->target);
        if (matches == 0) {
            continue;
        }

        if (args->collect_instances) {
            if (!append_instances(&args->result, &instance_capacity, line,
                                  matches)) {
                args->failed = true;
            }
        } else {
            args->result.count += matches;
        }
    }

    free(line);
    fclose(file);
    if (args->failed) {
        clear_result(&args->result);
    }
}

static struct count_result run_parallel(char *filename, char *target,
                                        bool collect_instances) {
    struct count_result combined = {0, NULL};
    if (filename == NULL || target == NULL || target[0] == '\0') {
        return combined;
    }

    struct stat file_info;
    if (stat(filename, &file_info) != 0 || file_info.st_size == 0) {
        return combined;
    }

    struct worker_args args[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        args[i].filename = filename;
        args[i].target = target;
        args[i].start = file_info.st_size * i / THREAD_COUNT;
        args[i].end = file_info.st_size * (i + 1) / THREAD_COUNT;
        args[i].collect_instances = collect_instances;
        args[i].failed = false;
        args[i].result.count = 0;
        args[i].result.instances = NULL;
    }

    #pragma omp parallel for num_threads(THREAD_COUNT) schedule(static)
    for (int i = 0; i < THREAD_COUNT; i++) {
        search_chunk(&args[i]);
    }

    size_t total = 0;
    bool failed = false;
    for (int i = 0; i < THREAD_COUNT; i++) {
        total += (size_t)args[i].result.count;
        failed = failed || args[i].failed;
    }

    if (failed) {
        for (int i = 0; i < THREAD_COUNT; i++) {
            clear_result(&args[i].result);
        }
        return combined;
    }

    combined.count = (int)total;
    if (!collect_instances || total == 0) {
        return combined;
    }

    combined.instances = malloc(total * sizeof(*combined.instances));
    if (combined.instances == NULL) {
        combined.count = 0;
        for (int i = 0; i < THREAD_COUNT; i++) {
            clear_result(&args[i].result);
        }
        return combined;
    }

    size_t destination = 0;
    for (int i = 0; i < THREAD_COUNT; i++) {
        for (int j = 0; j < args[i].result.count; j++) {
            combined.instances[destination++] = args[i].result.instances[j];
        }
        free(args[i].result.instances);
    }

    return combined;
}

int search_count(char *filename, char *target) {
    return run_parallel(filename, target, false).count;
}


struct count_result search_instance(char *filename,char *target){
    return run_parallel(filename, target, true);
}
