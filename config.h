#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* Teclas de mídia (sem modificador). Cada uma roda um comando configurável. */
enum MediaCmd {
    MEDIA_VOLUME_UP,
    MEDIA_VOLUME_DOWN,
    MEDIA_VOLUME_MUTE,
    MEDIA_MIC_MUTE,
    MEDIA_BRIGHTNESS_UP,
    MEDIA_BRIGHTNESS_DOWN,
    MEDIA_PLAY,
    MEDIA_NEXT,
    MEDIA_PREV,
    MEDIA_COUNT
};

typedef struct {
    /* Comandos */
    char *terminal_cmd;
    char *launcher_cmd;

    /* Modificador dos atalhos: "super" ou "alt" */
    char *mod;

    /* Layout */
    int   gap;
    int   top_padding;
    int   resize_step;
    int   min_column_width;

    /* Bordas das janelas (desenhadas por dentro do espaço do layout, como no i3) */
    int      border_width;      /* 0 = sem borda */
    uint32_t border_active;     /* cor da janela focada (0xRRGGBB) */
    uint32_t border_inactive;   /* cor das demais janelas */

    /* Comandos das teclas de mídia (indexados por enum MediaCmd; vazio = tecla desativada) */
    char *media_cmd[MEDIA_COUNT];

    /* Barra (lemonbar + bar.sh) */
    char *bar_font;        /* vazio = fonte padrão do lemonbar */
    char *bar_bg;
    char *bar_fg;
    char *bar_accent;
    char *ws_active_bg;
    char *ws_active_fg;
    char *ws_inactive_bg;
} Config;

void config_init_defaults(Config *config);
void config_load(Config *config);
void config_free(Config *config);

/* Monta o caminho de um arquivo do 33wm em $XDG_CONFIG_HOME/33wm (ou ~/.config/33wm).
 * Retorna 0 em sucesso, -1 se não houver diretório ou o caminho não couber. */
int config_file_path(const char *name, char *out, size_t size);

#endif
