// layout.h
#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t id;
    int x, y, w, h;
    bool mapped;          /* o WM acha que a janela está mapeada */
    int pending_unmaps;   /* unmaps pedidos pelo WM ainda não recebidos como evento */
} Window;

typedef struct Column {
    Window *windows;
    int window_count;
    int active_win_idx;
    int width;
    int original_width;
    int is_maximized;
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
    int min_column_width;
    int is_fullscreen;
} Workspace;

void workspace_recalculate(Workspace *ws, int top_padding);
void workspace_focus_column(Workspace *ws, Column *target);
void workspace_insert_column_after(Workspace *ws, Column *new_col, Column *target);
void workspace_remove_column(Workspace *ws, Column *target);
void workspace_add_window(Workspace *ws, uint32_t win_id);

// Novas funções para gerenciamento vertical
void workspace_remove_window_by_id(Workspace *ws, uint32_t win_id);
void column_focus_next_window(Column *col);
void column_focus_prev_window(Column *col);
void workspace_consume_next_column(Workspace *ws);
void workspace_expel_active_window(Workspace *ws);
void workspace_resize_active_column(Workspace *ws, int delta);

// layout.h
void workspace_focus_prev_column(Workspace *ws);
void workspace_focus_next_column(Workspace *ws);
void column_focus_prev_window(Column *col);
void column_focus_next_window(Column *col);
void column_toggle_maximize(Workspace *ws, Column *col);
#endif