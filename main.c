#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "wm.h"
#include "layout.h"

static WM *global_wm = NULL;

/* Pipe que acorda o loop de eventos quando chega um sinal. Os handlers só escrevem nele
 * (é a única operação segura dentro de um handler). */
static int wake_pipe[2] = { -1, -1 };

static void handle_sigchld(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void handle_sighup(int sig) {
    (void)sig;
    char c = 'H';
    ssize_t r = write(wake_pipe[1], &c, 1);
    (void)r;
}

/* Roda cada linha do arquivo `autostart` (em ~/.config/33wm) como um comando.
 * Linhas vazias e comentários (#) são ignoradas. */
static void run_autostart(WM *wm) {
    char path[512];
    if (config_file_path("autostart", path, sizeof(path)) != 0) return;

    FILE *file = fopen(path, "r");
    if (!file) return;

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *cmd = line;
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        size_t len = strlen(cmd);
        while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) {
            cmd[--len] = '\0';
        }
        if (cmd[0] == '\0' || cmd[0] == '#') continue;
        spawn(wm, cmd);
    }
    fclose(file);
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
    if (pipe(wake_pipe) != 0) {
        perror("[WM] pipe");
        return EXIT_FAILURE;
    }

    signal(SIGCHLD, handle_sigchld);
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    signal(SIGHUP, handle_sighup);
    /* Escrever num X morto dá SIGPIPE, que mataria o WM sem limpar a barra. Ignorado aqui,
     * o erro aparece como conexão perdida e wm_run encerra com a limpeza normal. */
    signal(SIGPIPE, SIG_IGN);

    WM *wm = wm_init();
    if (!wm) {
        fprintf(stderr, "Fatal error: ins't possible to init XCB layer.\n");
        return EXIT_FAILURE;
    }
    wm_start_bar(wm);
    run_autostart(wm);

    global_wm = wm;
    printf("[WM] 33wm started. Waiting events from X server...\n");

    wm_run(wm, wake_pipe[0]);

    wm_cleanup(wm);
    return EXIT_SUCCESS;
}
