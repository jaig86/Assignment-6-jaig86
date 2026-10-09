#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <omp.h>


/*
 * Decide how many OpenMP workers to use.
 */
static int get_thread_count(void) {

    int threads = omp_get_max_threads();

    if (threads < 2) {
        threads = 2;
    }

    if (threads > 8) {
        threads = 8;
    }

    return threads;
}


/*
 * Characters that are considered part of a word.
 */
static int is_word_char(char c) {

    return isalnum((unsigned char)c) ||
           c == '\'';
}


/*
 * Count complete-word occurrences in one line.
 *
 * Matching is case-insensitive.
 */
static int count_word_in_line(
    const char *line,
    const char *target
) {

    int count = 0;

    size_t target_len = strlen(target);

    if (target_len == 0) {
        return 0;
    }

    const char *p = line;

    while (*p != '\0') {

        if (strncasecmp(
                p,
                target,
                target_len
            ) == 0) {

            int left_ok =
                (p == line) ||
                !is_word_char(*(p - 1));

            char after =
                p[target_len];

            int right_ok =
                (after == '\0') ||
                !is_word_char(after);

            if (left_ok && right_ok) {

                count++;

                p += target_len;

                continue;
            }
        }

        p++;
    }

    return count;
}


/*
 * Remove newline characters from the end of a line.
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
 * Result belonging to one OpenMP worker.
 *
 * Each worker has private writable data.
 */
struct partial_result {

    int count;

    char **instances;
};


/*
 * Position a worker at the first complete line
 * belonging to its byte range.
 */
static int prepare_chunk(
    FILE *file,
    long start
) {

    if (start == 0) {

        if (fseek(file, 0, SEEK_SET) != 0) {
            return -1;
        }

        return 0;
    }

    /*
     * Check whether start already falls immediately
     * after a newline.
     */
    if (fseek(
            file,
            start - 1,
            SEEK_SET
        ) != 0) {

        return -1;
    }

    int previous = fgetc(file);

    if (previous == '\n') {

        if (fseek(
                file,
                start,
                SEEK_SET
            ) != 0) {

            return -1;
        }

        return 0;
    }

    /*
     * Otherwise skip the partial line shared
     * with the preceding chunk.
     */
    if (fseek(
            file,
            start,
            SEEK_SET
        ) != 0) {

        return -1;
    }

    int c;

    while ((c = fgetc(file)) != EOF) {

        if (c == '\n') {
            break;
        }
    }

    return 0;
}


/*
 * Search one chunk and return only a count.
 */
static int search_count_chunk(
    char *filename,
    char *target,
    long start,
    long end
) {

    FILE *file = fopen(filename, "r");

    if (file == NULL) {
        return 0;
    }

    if (prepare_chunk(
            file,
            start
        ) != 0) {

        fclose(file);

        return 0;
    }

    char *line = NULL;

    size_t capacity = 0;

    int count = 0;

    while (1) {

        long line_start =
            ftell(file);

        if (line_start < 0 ||
            line_start >= end) {

            break;
        }

        if (getline(
                &line,
                &capacity,
                file
            ) == -1) {

            break;
        }

        count +=
            count_word_in_line(
                line,
                target
            );
    }

    free(line);

    fclose(file);

    return count;
}


/*
 * Search one chunk and save the matching lines.
 */
static struct partial_result search_instance_chunk(
    char *filename,
    char *target,
    long start,
    long end
) {

    struct partial_result result;

    result.count = 0;
    result.instances = NULL;

    FILE *file = fopen(filename, "r");

    if (file == NULL) {
        return result;
    }

    if (prepare_chunk(
            file,
            start
        ) != 0) {

        fclose(file);

        return result;
    }

    char *line = NULL;

    size_t capacity = 0;

    while (1) {

        long line_start =
            ftell(file);

        if (line_start < 0 ||
            line_start >= end) {

            break;
        }

        if (getline(
                &line,
                &capacity,
                file
            ) == -1) {

            break;
        }

        int matches =
            count_word_in_line(
                line,
                target
            );

        if (matches == 0) {
            continue;
        }

        strip_newline(line);

        for (int m = 0;
             m < matches;
             m++) {

            char **new_instances =
                realloc(
                    result.instances,
                    sizeof(char *) *
                    (result.count + 1)
                );

            if (new_instances == NULL) {

                for (int j = 0;
                     j < result.count;
                     j++) {

                    free(
                        result.instances[j]
                    );
                }

                free(result.instances);

                result.instances = NULL;
                result.count = 0;

                free(line);
                fclose(file);

                return result;
            }

            result.instances =
                new_instances;

            result.instances[
                result.count
            ] = strdup(line);

            if (result.instances[
                    result.count
                ] == NULL) {

                for (int j = 0;
                     j < result.count;
                     j++) {

                    free(
                        result.instances[j]
                    );
                }

                free(result.instances);

                result.instances = NULL;
                result.count = 0;

                free(line);
                fclose(file);

                return result;
            }

            result.count++;
        }
    }

    free(line);

    fclose(file);

    return result;
}


/*
 * OpenMP implementation of search_count.
 */
int search_count(
    char *filename,
    char *target
) {

    struct stat st;

    if (stat(filename, &st) != 0) {
        return 0;
    }

    long file_size =
        (long)st.st_size;

    if (file_size <= 0) {
        return 0;
    }

    int thread_count =
        get_thread_count();

    if (file_size < thread_count) {
        thread_count = 1;
    }

    int *partial =
        calloc(
            thread_count,
            sizeof(int)
        );

    if (partial == NULL) {
        return 0;
    }

    long chunk =
        file_size / thread_count;


    /*
     * OpenMP automatically creates and manages
     * the worker threads for this loop.
     */
    #pragma omp parallel for num_threads(thread_count)
    for (int i = 0;
         i < thread_count;
         i++) {

        long start =
            i * chunk;

        long end;

        if (i ==
            thread_count - 1) {

            end = file_size;
        }
        else {

            end =
                (i + 1) * chunk;
        }

        /*
         * Each iteration writes only to partial[i].
         * Therefore no mutex is needed.
         */
        partial[i] =
            search_count_chunk(
                filename,
                target,
                start,
                end
            );
    }


    /*
     * We are outside the parallel region now.
     * Combine worker results serially.
     */
    int total = 0;

    for (int i = 0;
         i < thread_count;
         i++) {

        total += partial[i];
    }

    free(partial);

    return total;
}


/*
 * OpenMP implementation of search_instance.
 */
struct count_result search_instance(
    char *filename,
    char *target
) {

    struct count_result result;

    result.count = 0;
    result.instances = NULL;


    struct stat st;

    if (stat(filename, &st) != 0) {
        return result;
    }

    long file_size =
        (long)st.st_size;

    if (file_size <= 0) {
        return result;
    }

    int thread_count =
        get_thread_count();

    if (file_size < thread_count) {
        thread_count = 1;
    }


    struct partial_result *partial =
        calloc(
            thread_count,
            sizeof(struct partial_result)
        );

    if (partial == NULL) {
        return result;
    }


    long chunk =
        file_size / thread_count;


    /*
     * Each loop iteration processes a different
     * section of the file.
     */
    #pragma omp parallel for num_threads(thread_count)
    for (int i = 0;
         i < thread_count;
         i++) {

        long start =
            i * chunk;

        long end;

        if (i ==
            thread_count - 1) {

            end = file_size;
        }
        else {

            end =
                (i + 1) * chunk;
        }

        /*
         * Each thread writes to a separate
         * partial[i], avoiding a data race.
         */
        partial[i] =
            search_instance_chunk(
                filename,
                target,
                start,
                end
            );
    }


    /*
     * Count all results after the parallel region.
     */
    int total = 0;

    for (int i = 0;
         i < thread_count;
         i++) {

        total +=
            partial[i].count;
    }


    if (total > 0) {

        result.instances =
            malloc(
                sizeof(char *) *
                total
            );

        if (result.instances == NULL) {

            for (int i = 0;
                 i < thread_count;
                 i++) {

                for (int j = 0;
                     j < partial[i].count;
                     j++) {

                    free(
                        partial[i]
                        .instances[j]
                    );
                }

                free(
                    partial[i]
                    .instances
                );
            }

            free(partial);

            return result;
        }
    }


    /*
     * Merge chunk results in chunk order.
     * This also keeps the original file order.
     */
    int index = 0;

    for (int i = 0;
         i < thread_count;
         i++) {

        for (int j = 0;
             j < partial[i].count;
             j++) {

            result.instances[index++] =
                partial[i]
                .instances[j];
        }

        /*
         * Strings now belong to result.instances.
         * Only free the worker's pointer array.
         */
        free(
            partial[i]
            .instances
        );
    }


    result.count =
        total;

    free(partial);

    return result;
}