#include <stdio.h>
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