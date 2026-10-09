#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>


/*
 * Number of worker threads.
 *
 * We will normally use the number of available CPUs,
 * with a minimum of 2 and a maximum of 8.
 */
static int get_thread_count(void) {

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);

    if (cpus < 2) {
        return 2;
    }

    if (cpus > 8) {
        return 8;
    }

    return (int)cpus;
}


/*
 * Defines what characters are considered part of a word.
 */
static int is_word_char(char c) {

    return isalnum((unsigned char)c) ||
           c == '\'';
}


/*
 * Count occurrences of target in a line.
 *
 * Matching is case-insensitive and requires complete
 * word boundaries.
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
 * Remove newline / carriage-return characters
 * from the end of a line.
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
 * Arguments/results belonging to one worker.
 *
 * Each worker owns its own result data.
 * Nothing here is shared for writing between workers.
 */
struct worker_args {

    char *filename;
    char *target;

    long start;
    long end;

    int count;

    char **instances;
};


/*
 * Position a worker at the beginning of its chunk.
 *
 * If this is not the first chunk, skip the partial
 * line that started in the previous worker's chunk.
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
     * Look at the byte before start.
     */
    if (fseek(file, start - 1, SEEK_SET) != 0) {
        return -1;
    }

    int previous = fgetc(file);

    /*
     * If start is already immediately after a newline,
     * then it begins on a proper line boundary.
     */
    if (previous == '\n') {

        if (fseek(file, start, SEEK_SET) != 0) {
            return -1;
        }

        return 0;
    }

    /*
     * Otherwise skip the remainder of this partial line.
     */
    if (fseek(file, start, SEEK_SET) != 0) {
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
 * Worker used by search_count().
 */
static void *count_worker(void *arg) {

    struct worker_args *args =
        (struct worker_args *)arg;

    args->count = 0;

    FILE *file =
        fopen(args->filename, "r");

    if (file == NULL) {
        return NULL;
    }

    if (prepare_chunk(
            file,
            args->start
        ) != 0) {

        fclose(file);

        return NULL;
    }

    char *line = NULL;

    size_t capacity = 0;

    while (1) {

        /*
         * Record where this line begins.
         */
        long line_start =
            ftell(file);

        if (line_start < 0) {
            break;
        }

        /*
         * A worker only owns lines that BEGIN
         * inside its assigned byte range.
         */
        if (line_start >= args->end) {
            break;
        }

        if (getline(
                &line,
                &capacity,
                file
            ) == -1) {

            break;
        }

        args->count +=
            count_word_in_line(
                line,
                args->target
            );
    }

    free(line);

    fclose(file);

    return NULL;
}


/*
 * Worker used by search_instance().
 */
static void *instance_worker(void *arg) {

    struct worker_args *args =
        (struct worker_args *)arg;

    args->count = 0;
    args->instances = NULL;

    FILE *file =
        fopen(args->filename, "r");

    if (file == NULL) {
        return NULL;
    }

    if (prepare_chunk(
            file,
            args->start
        ) != 0) {

        fclose(file);

        return NULL;
    }

    char *line = NULL;

    size_t capacity = 0;

    while (1) {

        long line_start =
            ftell(file);

        if (line_start < 0) {
            break;
        }

        if (line_start >= args->end) {
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
                args->target
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
                    args->instances,
                    sizeof(char *) *
                    (args->count + 1)
                );

            if (new_instances == NULL) {

                for (int j = 0;
                     j < args->count;
                     j++) {

                    free(args->instances[j]);
                }

                free(args->instances);

                args->instances = NULL;
                args->count = 0;

                free(line);
                fclose(file);

                return NULL;
            }

            args->instances =
                new_instances;

            args->instances[
                args->count
            ] = strdup(line);

            if (args->instances[
                    args->count
                ] == NULL) {

                for (int j = 0;
                     j < args->count;
                     j++) {

                    free(args->instances[j]);
                }

                free(args->instances);

                args->instances = NULL;
                args->count = 0;

                free(line);
                fclose(file);

                return NULL;
            }

            args->count++;
        }
    }

    free(line);

    fclose(file);

    return NULL;
}


/*
 * Parallel count search.
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

    /*
     * For tiny files there is no reason to launch
     * more threads than useful chunks.
     */
    if (file_size < thread_count) {
        thread_count = 1;
    }

    pthread_t *threads =
        malloc(
            sizeof(pthread_t) *
            thread_count
        );

    struct worker_args *args =
        calloc(
            thread_count,
            sizeof(struct worker_args)
        );

    if (threads == NULL ||
        args == NULL) {

        free(threads);
        free(args);

        return 0;
    }

    long chunk =
        file_size / thread_count;

    for (int i = 0;
         i < thread_count;
         i++) {

        args[i].filename =
            filename;

        args[i].target =
            target;

        args[i].start =
            i * chunk;

        if (i ==
            thread_count - 1) {

            args[i].end =
                file_size;
        }
        else {

            args[i].end =
                (i + 1) * chunk;
        }

        pthread_create(
            &threads[i],
            NULL,
            count_worker,
            &args[i]
        );
    }

    int total = 0;

    for (int i = 0;
         i < thread_count;
         i++) {

        pthread_join(
            threads[i],
            NULL
        );

        total +=
            args[i].count;
    }

    free(threads);
    free(args);

    return total;
}


/*
 * Parallel instance search.
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

    pthread_t *threads =
        malloc(
            sizeof(pthread_t) *
            thread_count
        );

    struct worker_args *args =
        calloc(
            thread_count,
            sizeof(struct worker_args)
        );

    if (threads == NULL ||
        args == NULL) {

        free(threads);
        free(args);

        return result;
    }

    long chunk =
        file_size / thread_count;

    for (int i = 0;
         i < thread_count;
         i++) {

        args[i].filename =
            filename;

        args[i].target =
            target;

        args[i].start =
            i * chunk;

        if (i ==
            thread_count - 1) {

            args[i].end =
                file_size;
        }
        else {

            args[i].end =
                (i + 1) * chunk;
        }

        pthread_create(
            &threads[i],
            NULL,
            instance_worker,
            &args[i]
        );
    }

    /*
     * Join workers first.
     *
     * Each worker has only modified its own args[i],
     * so there has been no shared writable count/array.
     */
    for (int i = 0;
         i < thread_count;
         i++) {

        pthread_join(
            threads[i],
            NULL
        );
    }

    /*
     * Count total number of instances.
     */
    int total = 0;

    for (int i = 0;
         i < thread_count;
         i++) {

        total +=
            args[i].count;
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
                     j < args[i].count;
                     j++) {

                    free(
                        args[i]
                        .instances[j]
                    );
                }

                free(
                    args[i]
                    .instances
                );
            }

            free(threads);
            free(args);

            return result;
        }
    }

    /*
     * Merge in thread/chunk order.
     *
     * Because chunks are ordered from the beginning
     * to the end of the file, this also preserves
     * file order.
     */
    int index = 0;

    for (int i = 0;
         i < thread_count;
         i++) {

        for (int j = 0;
             j < args[i].count;
             j++) {

            result.instances[index++] =
                args[i].instances[j];
        }

        /*
         * Free only the pointer array.
         *
         * The strings themselves now belong to result.
         */
        free(
            args[i].instances
        );
    }

    result.count =
        total;

    free(threads);
    free(args);

    return result;
}