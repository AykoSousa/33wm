#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "config.h"

static char *trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void config_init_defaults(Config *config) {
    if (!config) return;
    config->launcher_cmd = strdup("rofi -show drun -show-icons");
    config->terminal_cmd = strdup("konsole");
    config->gap = 8;
    config->top_padding = 26;
}

void config_free(Config *config) {
    if (!config) return;
    if (config->launcher_cmd) free(config->launcher_cmd);
    if (config->terminal_cmd) free(config->terminal_cmd);
}

void config_load(Config *config) {
    config_init_defaults(config);

    char path[512];
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");

    if (xdg_config && strlen(xdg_config) > 0) {
        snprintf(path, sizeof(path), "%s/33wm/config", xdg_config);
    } else if (home && strlen(home) > 0) {
        snprintf(path, sizeof(path), "%s/.config/33wm/config", home);
    } else {
        return;
    }

    FILE *file = fopen(path, "r");
    if (!file) return;

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char *trimmed = trim(line);
        if (trimmed[0] == '#' || trimmed[0] == '\0') continue;

        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(trimmed);
        char *val = trim(eq + 1);

        if (strcmp(key, "terminal") == 0) {
            free(config->terminal_cmd);
            config->terminal_cmd = strdup(val);
        } else if (strcmp(key, "launcher") == 0) {
            free(config->launcher_cmd);
            config->launcher_cmd = strdup(val);
        } else if (strcmp(key, "gap") == 0) {
            config->gap = atoi(val);
        } else if (strcmp(key, "top_padding") == 0) {
            config->top_padding = atoi(val);
        }
    }

    fclose(file);
}