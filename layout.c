#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "layout.h"

void workspace_recalculate(Workspace *ws) {
    int current_x = -ws->camera_x;
    
    for (Column *col = ws->first_col; col != NULL; col = col->next) {
        int usable_h = ws->screen_h - ((col->window_count + 1) * ws->gap);
        int win_h = usable_h / col->window_count;
        int current_y = ws->gap;

        for (int i = 0; i < col->window_count; i++) {
            col->windows[i].x = current_x + ws->gap;
            col->windows[i].y = current_y;
            col->windows[i].w = col->width - (2 * ws->gap);
            col->windows[i].h = win_h;

            current_y += win_h + ws->gap;
        }
        current_x += col->width;
    }
}

int column_get_absolute_x(const Workspace *ws, const Column *target) {
    int abs_x = 0;
    for (Column *col = ws->first_col; col != NULL; col = col->next) {
        if (col == target) return abs_x;
        abs_x += col->width;
    }
    return -1;
}

void workspace_focus_column(Workspace *ws, Column *target) {
    if (!target) return;
    ws->active_col = target;

    int abs_x = column_get_absolute_x(ws, target);
    int center_col = abs_x + (target->width / 2);
    int center_screen = ws->screen_w / 2;

    int target_camera_x = center_col - center_screen;

    if (target_camera_x < 0) {
        target_camera_x = 0;
    }

    ws->camera_x = target_camera_x;
    workspace_recalculate(ws);
}

void workspace_insert_column_after(Workspace *ws, Column *new_col, Column *target) {
    if (!ws || !new_col) return;

    if (ws->first_col == NULL) {
        ws->first_col = new_col;
        new_col->prev = NULL;
        new_col->next = NULL;
    } 

    else if (target != NULL) {
        new_col->next = target->next;
        new_col->prev = target;
        if (target->next) {
            target->next->prev = new_col;
        }
        target->next = new_col;
    }

    workspace_focus_column(ws, new_col);
}

void workspace_remove_column(Workspace *ws, Column *target) {
    if (!ws || !target) return;

    Column *fallback_focus = target->next ? target->next : target->prev;

    if (target->prev) {
        target->prev->next = target->next;
    } else {
        ws->first_col = target->next;
    }

    if (target->next) {
        target->next->prev = target->prev;
    }

    target->next = NULL;
    target->prev = NULL;

    if (ws->active_col == target) {
        if (fallback_focus) {
            workspace_focus_column(ws, fallback_focus);
        } else {
            ws->active_col = NULL;
            ws->camera_x = 0;
            workspace_recalculate(ws);
        }
    } else {
        workspace_recalculate(ws);
    }
}

void column_focus_next_window(Column *col) {
    if (!col || col->window_count <= 1) return;
    col->active_win_idx = (col->active_win_idx + 1) % col->window_count;
}

void column_focus_prev_window(Column *col) {
    if (!col || col->window_count <= 1) return;
    col->active_win_idx = (col->active_win_idx - 1 + col->window_count) % col->window_count;
}

void workspace_remove_window_by_id(Workspace *ws, uint32_t win_id) {
    if (!ws) return;

    for (Column *col = ws->first_col; col != NULL; col = col->next) {
        for (int i = 0; i < col->window_count; i++) {
            if (col->windows[i].id == win_id) {
                if (col->window_count == 1) {
                    workspace_remove_column(ws, col);
                    free(col->windows);
                    free(col);
                    return;
                }

                for (int j = i; j < col->window_count - 1; j++) {
                    col->windows[j] = col->windows[j + 1];
                }
                col->window_count--;
                col->windows = realloc(col->windows, sizeof(Window) * col->window_count);

                if (col->active_win_idx >= col->window_count) {
                    col->active_win_idx = col->window_count - 1;
                }

                workspace_recalculate(ws);
                return;
            }
        }
    }
}

void workspace_consume_next_column(Workspace *ws) {
    if (!ws || !ws->active_col || !ws->active_col->next) return;

    Column *curr = ws->active_col;
    Column *next = curr->next;

    int new_count = curr->window_count + next->window_count;
    curr->windows = realloc(curr->windows, sizeof(Window) * new_count);

    for (int i = 0; i < next->window_count; i++) {
        curr->windows[curr->window_count + i] = next->windows[i];
    }
    curr->window_count = new_count;

    workspace_remove_column(ws, next);
    free(next->windows);
    free(next);

    workspace_recalculate(ws);
}

void workspace_expel_active_window(Workspace *ws) {
    if (!ws || !ws->active_col || ws->active_col->window_count <= 1) return;

    Column *curr = ws->active_col;
    int idx = curr->active_win_idx;
    Window target_win = curr->windows[idx];

    for (int i = idx; i < curr->window_count - 1; i++) {
        curr->windows[i] = curr->windows[i + 1];
    }
    curr->window_count--;
    curr->windows = realloc(curr->windows, sizeof(Window) * curr->window_count);
    if (curr->active_win_idx >= curr->window_count) {
        curr->active_win_idx = curr->window_count - 1;
    }

    Window *new_win_arr = malloc(sizeof(Window));
    *new_win_arr = target_win;

    Column *new_col = calloc(1, sizeof(Column));
    new_col->windows = new_win_arr;
    new_col->window_count = 1;
    new_col->width = ws->screen_w / 2;

    workspace_insert_column_after(ws, new_col, curr);
}

void workspace_resize_active_column(Workspace *ws, int delta) {
    if (!ws || !ws->active_col) return;

    int min_width = 200;
    int max_width = ws->screen_w;

    int new_width = ws->active_col->width + delta;

    if (new_width < min_width) new_width = min_width;
    if (new_width > max_width) new_width = max_width;

    ws->active_col->width = new_width;

    workspace_focus_column(ws, ws->active_col);
}