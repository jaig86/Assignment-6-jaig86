#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int main(int argc, char** argv) {

    if (argc != 4) {

        fprintf(
            stderr,
            "Usage: %s <count|instance> <input_file> <target_word>\n",
            argv[0]
        );

        return 1;
    }

    char *mode = argv[1];
    char *filename = argv[2];
    char *target = argv[3];


    if (strcmp(mode, "count") == 0) {

        int count =
            search_count(filename, target);

        printf(
            "Found: %d of %s in %s\n",
            count,
            target,
            filename
        );

        return 0;
    }


    if (strcmp(mode, "instance") == 0) {

        struct count_result res =
            search_instance(filename, target);

        printf(
            "Found: %d of %s in %s\n",
            res.count,
            target,
            filename
        );

        for (int i = 0; i < res.count; i++) {

            printf(
                "res.instances[%d]: %s\n",
                i,
                res.instances[i]
            );
        }

        /*
         * Free the strings returned by search_instance.
         */
        for (int i = 0; i < res.count; i++) {
            free(res.instances[i]);
        }

        free(res.instances);

        return 0;
    }


    fprintf(
        stderr,
        "Unknown mode: %s\n",
        mode
    );

    return 1;
}