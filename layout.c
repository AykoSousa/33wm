#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "layout.h"

static void workspace_center_on_column(Workspace *ws, Column *target) {
    if (!ws || !target) return;
    int col_x = 0;
    for (Column *c = ws->first_col; c != NULL && c != target; c = c->next) {
        col_x += c->width;
    }
    ws->camera_x = col_x;
}

void column_toggle_maximize(Workspace *ws, Column *col) {
    if (!col || !ws) return;

    col->is_maximized = !col->is_maximized;

    if (col->is_maximized) {
        // Salva a largura atual antes de expandir
        col->original_width = col->width;
        // Expande a coluna para cobrir a LARGURA TOTAL da tela
        col->width = ws->screen_w;
    } else {
        // Restaura a largura antiga (ou metade da tela caso não tenha registro)
        if (col->original_width > 0) {
            col->width = col->original_width;
        } else {
            col->width = ws->screen_w / 2;
        }
    }
}

void workspace_recalculate(Workspace *ws, int top_padding) {
    if (!ws || !ws->first_col) return;

    int avail_h = ws->screen_h - top_padding;

    // Fullscreen estrito (i3 - Super + F)
    if (ws->is_fullscreen && ws->active_col && ws->active_col->window_count > 0) {
        int active_idx = ws->active_col->active_win_idx;
        Window *w = &ws->active_col->windows[active_idx];
        w->x = 0;
        w->y = top_padding;
        w->w = ws->screen_w;
        w->h = avail_h;
        return;
    }

    // Se a coluna ativa estiver maximizada, alinha a câmera nela
    if (ws->active_col && ws->active_col->is_maximized) {
        workspace_center_on_column(ws, ws->active_col);
    }

    int current_x = -ws->camera_x;
    for (Column *col = ws->first_col; col != NULL; col = col->next) {
        if (col->is_maximized && col->window_count > 0) {
            // Janela ativa ganha 100% da LARGURA e 100% da ALTURA
            int active_idx = col->active_win_idx;
            for (int i = 0; i < col->window_count; i++) {
                Window *w = &col->windows[i];
                if (i == active_idx) {
                    w->x = current_x + ws->gap;
                    w->y = top_padding + ws->gap;
                    w->w = col->width - (2 * ws->gap); // Largura total
                    w->h = avail_h - (2 * ws->gap);    // Altura total
                }
            }
        } else {
            // Layout de mosaico normal
            int usable_h = avail_h - ((col->window_count + 1) * ws->gap);
            int win_h = usable_h / col->window_count;
            int current_y = top_padding + ws->gap;

            for (int i = 0; i < col->window_count; i++) {
                Window *w = &col->windows[i];
                w->x = current_x + ws->gap;
                w->y = current_y;
                w->w = col->width - (2 * ws->gap);
                w->h = win_h;

                current_y += win_h + ws->gap;
            }
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
    workspace_recalculate(ws, 0);
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
            workspace_recalculate(ws, 0);
        }
    } else {
        workspace_recalculate(ws, 0);
    }
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

                workspace_recalculate(ws, 0);
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

    workspace_recalculate(ws, 0);
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

void workspace_add_window(Workspace *ws, uint32_t win_id) {
    if (!ws) return;

    // Aloca a nova janela
    Window *win_arr = malloc(sizeof(Window));
    win_arr[0].id = win_id;
    win_arr[0].x = 0;
    win_arr[0].y = 0;
    win_arr[0].w = 0;
    win_arr[0].h = 0;

    // Cria a coluna para essa janela (com largura padrão de metade da tela)
    Column *new_col = calloc(1, sizeof(Column));
    new_col->windows = win_arr;
    new_col->window_count = 1;
    new_col->active_win_idx = 0;
    new_col->width = ws->screen_w / 2;

    // Insere a coluna no workspace e passa o foco para ela
    workspace_insert_column_after(ws, new_col, ws->active_col);
    workspace_focus_column(ws, new_col);
}

// Alterna o foco para a coluna à esquerda, recentrando a câmera nela
// (sem isso, fora do fullscreen, a coluna focada pode ficar fora da tela)
void workspace_focus_prev_column(Workspace *ws) {
    if (!ws || !ws->active_col || !ws->active_col->prev) return;
    workspace_focus_column(ws, ws->active_col->prev);
}

// Alterna o foco para a coluna à direita, recentrando a câmera nela
void workspace_focus_next_column(Workspace *ws) {
    if (!ws || !ws->active_col || !ws->active_col->next) return;
    workspace_focus_column(ws, ws->active_col->next);
}

// Alterna o foco para a janela acima na coluna atual
void column_focus_prev_window(Column *col) {
    if (!col || col->window_count <= 1) return;
    if (col->active_win_idx > 0) {
        col->active_win_idx--;
    }
}

// Alterna o foco para a janela abaixo na coluna atual
void column_focus_next_window(Column *col) {
    if (!col || col->window_count <= 1) return;
    if (col->active_win_idx < col->window_count - 1) {
        col->active_win_idx++;
    }
}