#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>

#define MAX_LINE_LENGTH 256

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

static void free_instances(struct count_result *result) {
    for (int i = 0; i < result->count; i++) {
        free(result->instances[i]);
    }
    free(result->instances);
    result->instances = NULL;
    result->count = 0;
}

int search_count(char *filename, char *target) {
    if (filename == NULL || target == NULL || target[0] == '\0') {
        return 0;
    }

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return 0;
    }

    int count = 0;
    size_t capacity = MAX_LINE_LENGTH;
    size_t length = 0;
    char *line = malloc(capacity);
    if (line == NULL) {
        fclose(file);
        return 0;
    }

    int character;
    while ((character = fgetc(file)) != EOF) {
        if (character == '\n') {
            line[length] = '\0';
            count += matches_in_line(line, target);
            length = 0;
            continue;
        }

        if (length + 1 >= capacity) {
            size_t new_capacity = capacity * 2;
            char *resized = realloc(line, new_capacity);
            if (resized == NULL) {
                free(line);
                fclose(file);
                return 0;
            }
            line = resized;
            capacity = new_capacity;
        }
        line[length++] = (char)character;
    }

    if (length > 0) {
        line[length] = '\0';
        count += matches_in_line(line, target);
    }

    free(line);
    fclose(file);
    return count;
}

struct count_result search_instance(char *filename,char *target){
    struct count_result result = {0, NULL};
    if (filename == NULL || target == NULL || target[0] == '\0') {
        return result;
    }

    int expected_count = search_count(filename, target);
    if (expected_count == 0) {
        return result;
    }

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return result;
    }

    result.instances = malloc((size_t)expected_count *
                              sizeof(*result.instances));
    if (result.instances == NULL) {
        fclose(file);
        return result;
    }

    size_t line_capacity = MAX_LINE_LENGTH;
    char *line = malloc(line_capacity);
    if (line == NULL) {
        free(result.instances);
        result.instances = NULL;
        fclose(file);
        return result;
    }

    while (getline(&line, &line_capacity, file) != -1) {
        int matches = matches_in_line(line, target);
        if (matches == 0) {
            continue;
        }

        for (int i = 0; i < matches; i++) {
            result.instances[result.count] = copy_trimmed_line(line);
            if (result.instances[result.count] == NULL) {
                free_instances(&result);
                break;
            }
            result.count++;
        }

        if (result.instances == NULL) {
            break;
        }
    }

    free(line);
    fclose(file);
    return result;
}
