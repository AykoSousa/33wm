#ifndef LY_H
#define LY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t id;
    int x, y, w, h;
} Window;

typedef struct Column {
    Window *windows;
    int window_count;
    int width;
    struct Column *next;
    struct Column *prev;
} Column;

typedef struct {
    Column *first_col;
    Column *active_col;
    int camera_x;
    int screen_w;
    int screen_h;
    int gap;
} Workspace;

void workspace_recalculate(Workspace *ws);
void workspace_focus_column(Workspace *ws, Column *target);
void workspace_insert_column_after(Workspace *ws, Column *new_col, Column *target);
void workspace_remove_column(Workspace *ws, Column *target);

#endif