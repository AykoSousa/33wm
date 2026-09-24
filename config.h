#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    char *launcher_cmd;
    char *terminal_cmd;
    int   gap;
    int   top_padding;
} Config;

void config_init_defaults(Config *config);
void config_load(Config *config);
void config_free(Config *config);

#endif