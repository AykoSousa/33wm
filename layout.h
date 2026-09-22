// layout.h
#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t id;
    int x, y, w, h;
} Window;

typedef struct Column {
    Window *windows;
    int window_count;
    int active_win_idx;
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

// Novas funções para gerenciamento vertical
void workspace_remove_window_by_id(Workspace *ws, uint32_t win_id);
void column_focus_next_window(Column *col);
void column_focus_prev_window(Column *col);
void workspace_consume_next_column(Workspace *ws);
void workspace_expel_active_window(Workspace *ws);
void workspace_resize_active_column(Workspace *ws, int delta);

#endif