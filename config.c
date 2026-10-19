#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
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

static void set_str(char **field, const char *val) {
    free(*field);
    *field = strdup(val);
}

// Converte val em inteiro (>= min). Retorna false se o valor for inválido.
static bool parse_int(const char *val, int min, int *out) {
    char *end;
    long n = strtol(val, &end, 10);
    if (end == val || *end != '\0' || n < min || n > 100000) return false;
    *out = (int)n;
    return true;
}

// Aceita cores no formato #RRGGBB
static bool is_color(const char *val) {
    if (val[0] != '#' || strlen(val) != 7) return false;
    for (int i = 1; i < 7; i++) {
        if (!isxdigit((unsigned char)val[i])) return false;
    }
    return true;
}

// Converte uma cor já validada (#RRGGBB) para 0xRRGGBB
static uint32_t parse_color(const char *val) {
    return (uint32_t)strtoul(val + 1, NULL, 16);
}

/* Chave do arquivo e comando padrão de cada tecla de mídia, na ordem de enum MediaCmd */
static const struct {
    const char *key;
    const char *def;
} media_defs[MEDIA_COUNT] = {
    { "volume_up_cmd",       "pactl set-sink-volume @DEFAULT_SINK@ +5%" },
    { "volume_down_cmd",     "pactl set-sink-volume @DEFAULT_SINK@ -5%" },
    { "volume_mute_cmd",     "pactl set-sink-mute @DEFAULT_SINK@ toggle" },
    { "mic_mute_cmd",        "pactl set-source-mute @DEFAULT_SOURCE@ toggle" },
    { "brightness_up_cmd",   "brightnessctl set +5%" },
    { "brightness_down_cmd", "brightnessctl set 5%-" },
    { "media_play_cmd",      "playerctl play-pause" },
    { "media_next_cmd",      "playerctl next" },
    { "media_prev_cmd",      "playerctl previous" },
};

void config_init_defaults(Config *config) {
    if (!config) return;
    for (int i = 0; i < MEDIA_COUNT; i++) {
        config->media_cmd[i] = strdup(media_defs[i].def);
    }
    config->terminal_cmd = strdup("konsole");
    config->launcher_cmd = strdup("rofi -show drun -show-icons");
    config->mod = strdup("super");

    config->gap = 8;
    config->top_padding = 26;
    config->resize_step = 50;
    config->min_column_width = 200;

    config->border_width = 1;
    config->border_active = 0x7aa2f7;
    config->border_inactive = 0x3b4252;

    config->root_bg = 0x1a1b26;

    config->bar_font = strdup("");
    config->bar_bg = strdup("#1a1b26");
    config->bar_fg = strdup("#a9b1d6");
    config->bar_accent = strdup("#bb9af7");
    config->ws_active_bg = strdup("#7aa2f7");
    config->ws_active_fg = strdup("#15161e");
    config->ws_inactive_bg = strdup("#24283b");
}

void config_free(Config *config) {
    if (!config) return;
    for (int i = 0; i < MEDIA_COUNT; i++) {
        free(config->media_cmd[i]);
    }
    free(config->terminal_cmd);
    free(config->launcher_cmd);
    free(config->mod);
    free(config->bar_font);
    free(config->bar_bg);
    free(config->bar_fg);
    free(config->bar_accent);
    free(config->ws_active_bg);
    free(config->ws_active_fg);
    free(config->ws_inactive_bg);
}

// Aplica uma linha "chave = valor". Valores inválidos mantêm o padrão e geram aviso.
static void apply_setting(Config *config, const char *key, const char *val, int lineno) {
    int n;

    for (int i = 0; i < MEDIA_COUNT; i++) {
        if (strcmp(key, media_defs[i].key) == 0) {
            set_str(&config->media_cmd[i], val);
            return;
        }
    }

    if (strcmp(key, "terminal") == 0) {
        set_str(&config->terminal_cmd, val);
    } else if (strcmp(key, "launcher") == 0) {
        set_str(&config->launcher_cmd, val);
    } else if (strcmp(key, "mod") == 0) {
        if (strcmp(val, "super") == 0 || strcmp(val, "alt") == 0) {
            set_str(&config->mod, val);
        } else {
            goto invalid;
        }
    } else if (strcmp(key, "gap") == 0) {
        if (!parse_int(val, 0, &n)) goto invalid;
        config->gap = n;
    } else if (strcmp(key, "top_padding") == 0) {
        if (!parse_int(val, 0, &n)) goto invalid;
        config->top_padding = n;
    } else if (strcmp(key, "resize_step") == 0) {
        if (!parse_int(val, 1, &n)) goto invalid;
        config->resize_step = n;
    } else if (strcmp(key, "min_column_width") == 0) {
        if (!parse_int(val, 1, &n)) goto invalid;
        config->min_column_width = n;
    } else if (strcmp(key, "border_width") == 0) {
        if (!parse_int(val, 0, &n)) goto invalid;
        config->border_width = n;
    } else if (strcmp(key, "border_active") == 0) {
        if (!is_color(val)) goto invalid;
        config->border_active = parse_color(val);
    } else if (strcmp(key, "border_inactive") == 0) {
        if (!is_color(val)) goto invalid;
        config->border_inactive = parse_color(val);
    } else if (strcmp(key, "root_bg") == 0) {
        if (!is_color(val)) goto invalid;
        config->root_bg = parse_color(val);
    } else if (strcmp(key, "bar_font") == 0) {
        // A fonte vai para a linha de comando do shell, então não pode ter aspas simples
        if (strchr(val, '\'') != NULL) goto invalid;
        set_str(&config->bar_font, val);
    } else if (strcmp(key, "bar_bg") == 0) {
        if (!is_color(val)) goto invalid;
        set_str(&config->bar_bg, val);
    } else if (strcmp(key, "bar_fg") == 0) {
        if (!is_color(val)) goto invalid;
        set_str(&config->bar_fg, val);
    } else if (strcmp(key, "bar_accent") == 0) {
        if (!is_color(val)) goto invalid;
        set_str(&config->bar_accent, val);
    } else if (strcmp(key, "ws_active_bg") == 0) {
        if (!is_color(val)) goto invalid;
        set_str(&config->ws_active_bg, val);
    } else if (strcmp(key, "ws_active_fg") == 0) {
        if (!is_color(val)) goto invalid;
        set_str(&config->ws_active_fg, val);
    } else if (strcmp(key, "ws_inactive_bg") == 0) {
        if (!is_color(val)) goto invalid;
        set_str(&config->ws_inactive_bg, val);
    } else {
        fprintf(stderr, "[config] linha %d: chave desconhecida '%s' (ignorada)\n", lineno, key);
    }
    return;

invalid:
    fprintf(stderr, "[config] linha %d: valor inválido para '%s': '%s' (usando padrão)\n",
            lineno, key, val);
}

int config_file_path(const char *name, char *out, size_t size) {
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int n;

    if (xdg_config && strlen(xdg_config) > 0) {
        n = snprintf(out, size, "%s/33wm/%s", xdg_config, name);
    } else if (home && strlen(home) > 0) {
        n = snprintf(out, size, "%s/.config/33wm/%s", home, name);
    } else {
        return -1;
    }
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

void config_load(Config *config) {
    config_init_defaults(config);

    char path[512];
    if (config_file_path("config", path, sizeof(path)) != 0) return;

    FILE *file = fopen(path, "r");
    if (!file) return;

    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof(line), file)) {
        lineno++;
        char *trimmed = trim(line);
        if (trimmed[0] == '#' || trimmed[0] == '\0') continue;

        char *eq = strchr(trimmed, '=');
        if (!eq) {
            fprintf(stderr, "[config] linha %d: formato inválido, esperado 'chave = valor'\n", lineno);
            continue;
        }

        *eq = '\0';
        char *key = trim(trimmed);
        char *val = trim(eq + 1);

        // Comentário no fim da linha: '#' seguido de espaço e no início do valor ou
        // precedido de espaço. Cores (#1a1b26) não são afetadas, pois não têm espaço após o '#'.
        for (char *p = val; *p; p++) {
            if (*p == '#' && (p == val || isspace((unsigned char)p[-1])) &&
                (p[1] == '\0' || isspace((unsigned char)p[1]))) {
                *p = '\0';
                val = trim(val);
                break;
            }
        }

        apply_setting(config, key, val, lineno);
    }

    fclose(file);
}
