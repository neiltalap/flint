#include "flint/flint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// parse_frames handles "--frames N".
// Stores N in *out, or -1 when absent.
// Returns 0 on success, 1 on bad usage.
static int parse_frames(int argc,
        char *argv[], long *out) {
    *out = -1;
    if (argc == 1) {
        return (0);
    }
    if (argc != 3) {
        fprintf(stderr,
            "usage: flint [--frames N]\n");
        return (1);
    }
    if (strcmp(argv[1], "--frames") != 0) {
        fprintf(stderr,
            "usage: flint [--frames N]\n");
        return (1);
    }
    char *end = nullptr;
    long n = strtol(argv[2], &end, 10);
    if (end == argv[2] ||
            *end != '\0' || n <= 0) {
        fprintf(stderr,
            "frames must be N > 0\n");
        return (1);
    }
    *out = n;
    return (0);
}

// main creates the app, pumps frames
// until exit or the frame bound, then
// destroys it. A missing display is a
// headless skip, not a failure.
int main(int argc, char *argv[]) {
    long frames = -1;
    int bad =
        parse_frames(argc, argv, &frames);
    if (bad != 0) {
        return (1);
    }
    fl_app_t app = nullptr;
    fl_result_t rc = fl_app_create(&app);
    if (rc != FL_OK) {
        if (rc == FL_ERR_NODISPLAY) {
            printf("no display, skipping\n");
            return (0);
        }
        fprintf(stderr, "failed to create app\n");
        return (1);
    }
    while (frames != 0 &&
            fl_app_frame(app)) {
        if (frames > 0) {
            frames--;
        }
    }
    fl_app_destroy(app);
    return (0);
}
