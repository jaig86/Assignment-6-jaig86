#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256


/*
 * Return 1 if token exactly matches target.
 */
static int word_matches(const char *token, const char *target) {
    return strcmp(token, target) == 0;
}


/*
 * Remove newline characters from the end of a string.
 */
static void strip_newline(char *line) {

    size_t len = strlen(line);

    while (len > 0 &&
          (line[len - 1] == '\n' ||
           line[len - 1] == '\r')) {

        line[len - 1] = '\0';
        len--;
    }
}


/*
 * Count exact word occurrences in a file.
 */
int search_count(char *filename, char *target) {

    FILE *file = fopen(filename, "r");

    if (file == NULL) {
        return 0;
    }

    int count = 0;

    char line[MAX_LINE_LENGTH];

    while (fgets(line, sizeof(line), file) != NULL) {

        /*
         * Make a copy because strtok modifies its input.
         */
        char copy[MAX_LINE_LENGTH];
        strcpy(copy, line);

        char *token =
            strtok(
                copy,
                " \t\r\n.,!?;:\"()[]{}<>-"
            );

        while (token != NULL) {

            if (word_matches(token, target)) {
                count++;
            }

            token =
                strtok(
                    NULL,
                    " \t\r\n.,!?;:\"()[]{}<>-"
                );
        }
    }

    fclose(file);

    return count;
}


/*
 * Return the count plus one string entry for every
 * occurrence of target.
 */
struct count_result search_instance(
    char *filename,
    char *target
) {

    struct count_result result;

    result.count = 0;
    result.instances = NULL;

    FILE *file = fopen(filename, "r");

    if (file == NULL) {
        return result;
    }

    char line[MAX_LINE_LENGTH];

    while (fgets(line, sizeof(line), file) != NULL) {

        /*
         * Keep the original line for result.instances.
         */
        char original[MAX_LINE_LENGTH];
        strcpy(original, line);

        strip_newline(original);

        /*
         * Search a separate copy because strtok changes
         * the contents of the string.
         */
        char copy[MAX_LINE_LENGTH];
        strcpy(copy, line);

        char *token =
            strtok(
                copy,
                " \t\r\n.,!?;:\"()[]{}<>-"
            );

        while (token != NULL) {

            if (word_matches(token, target)) {

                char **new_instances =
                    realloc(
                        result.instances,
                        sizeof(char *) *
                        (result.count + 1)
                    );

                if (new_instances == NULL) {

                    for (int i = 0;
                         i < result.count;
                         i++) {

                        free(result.instances[i]);
                    }

                    free(result.instances);

                    result.instances = NULL;
                    result.count = 0;

                    fclose(file);

                    return result;
                }

                result.instances = new_instances;

                result.instances[result.count] =
                    strdup(original);

                if (result.instances[result.count] == NULL) {

                    for (int i = 0;
                         i < result.count;
                         i++) {

                        free(result.instances[i]);
                    }

                    free(result.instances);

                    result.instances = NULL;
                    result.count = 0;

                    fclose(file);

                    return result;
                }

                result.count++;
            }

            token =
                strtok(
                    NULL,
                    " \t\r\n.,!?;:\"()[]{}<>-"
                );
        }
    }

    fclose(file);

    return result;
}