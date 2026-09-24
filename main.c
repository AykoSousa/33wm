#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include "wm.h"
#include "layout.h"

static WM *global_wm = NULL;

static void handle_sigchld(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void handle_sigint(int sig) {
    (void)sig;
    if (global_wm) {
        wm_cleanup(global_wm);
    }
    printf("\n[WM] Finalized successfully.\n");
    exit(0);
}

int main(void) {
    signal(SIGCHLD, handle_sigchld);
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    WM *wm = wm_init();
    if (!wm) {
        fprintf(stderr, "Fatal error: ins't possible to init XCB layer.\n");
        return EXIT_FAILURE;
    }
    config_load(&wm->config);

    global_wm = wm;
    printf("[WM] 33wm started. Waiting events from X server...\n");

    wm_run(wm);

    wm_cleanup(wm);
    return EXIT_SUCCESS;
}