#ifndef WM_H
#define WM_H

#include <sys/types.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include "layout.h"
#include "config.h"

#define NUM_WORKSPACES 9
#define MAX_MONITORS 16

/* Janela flutuante: fica fora do layout em colunas, centralizada no workspace onde abriu */
typedef struct FloatWin {
    xcb_window_t id;
    int ws;
    bool mapped;
    int pending_unmaps;
    struct FloatWin *next;
} FloatWin;

/* Monitor físico: área na tela e o workspace que ele mostra. Cada workspace fica visível
 * em no máximo um monitor. */
typedef struct {
    int x, y, w, h;
    int ws;
} Monitor;

/* Arrastar uma janela flutuante com Super + botão (1 = mover, 3 = redimensionar) */
typedef struct {
    bool active;
    xcb_window_t win;
    uint8_t button;
    int16_t start_x, start_y;
    int orig_x, orig_y, orig_w, orig_h;
} DragState;

typedef struct {
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t root;
    xcb_key_symbols_t *keysyms;

    Workspace workspaces[NUM_WORKSPACES];
    int active_ws_idx;     /* workspace do monitor focado */
    Workspace *ws;         /* ponteiro para workspaces[active_ws_idx] */

    Monitor monitors[MAX_MONITORS];
    int monitor_count;
    int focused_mon;       /* índice do monitor com foco; monitors[0] é o principal (barra) */

    Config config;
    uint16_t mod;          /* máscara do modificador dos atalhos (Super ou Alt) */

    xcb_atom_t atom_wm_protocols;
    xcb_atom_t atom_wm_delete_window;

    xcb_atom_t atom_net_supported;
    xcb_atom_t atom_net_number_of_desktops;
    xcb_atom_t atom_net_current_desktop;
    xcb_atom_t atom_net_supporting_wm_check;
    xcb_atom_t atom_net_wm_name;
    xcb_atom_t atom_net_wm_window_type;
    xcb_atom_t atom_net_wm_window_type_dock;
    xcb_atom_t atom_net_wm_window_type_dialog;
    xcb_atom_t atom_net_wm_window_type_utility;
    xcb_atom_t atom_net_wm_window_type_splash;
    xcb_atom_t atom_net_wm_window_type_toolbar;
    xcb_atom_t atom_occupied_ws;
    xcb_atom_t atom_net_wm_state;
    xcb_atom_t atom_net_wm_state_fullscreen;
    xcb_atom_t atom_net_active_window;
    xcb_atom_t atom_net_client_list;
    xcb_window_t wm_check_win;

    uint8_t randr_event;   /* código do evento RRScreenChangeNotify (0 = RandR indisponível) */

    FloatWin *floats;          /* janelas flutuantes de todos os workspaces */
    xcb_window_t focus_float;  /* janela flutuante com foco (0 = foco no layout) */
    xcb_window_t close_pending; /* janela a que já foi pedido para fechar (0 = nenhuma) */
    pid_t bar_pid;              /* grupo de processos da barra (0 = não iniciada) */
    DragState drag;
} WM;

WM*  wm_init(void);
void wm_cleanup(WM *wm);
void wm_run(WM *wm, int wake_fd);
void wm_apply_layout(WM *wm);
void wm_close_active_window(WM *wm);

void wm_switch_workspace(WM *wm, int target_idx);
void wm_move_active_col_to_workspace(WM *wm, int target_idx);
void wm_focus_monitor(WM *wm, int delta);
void wm_move_col_to_monitor(WM *wm, int delta);
void wm_start_bar(WM *wm);
void wm_reload_settings(WM *wm);

/* Roda cmd via sh, em nova sessão. Retorna o PID do processo (0 se não rodou). */
pid_t spawn(WM *wm, const char *cmd);

void ewmh_init(WM *wm);
void ewmh_update_current_desktop(WM *wm);
void ewmh_update_occupied(WM *wm);
void wm_toggle_fullscreen(WM *wm);
void wm_toggle_column_maximize(WM *wm);

void wm_focus_prev_col(WM *wm);
void wm_focus_next_col(WM *wm);
void wm_focus_prev_win(WM *wm);
void wm_focus_next_win(WM *wm);
void wm_focus_active(WM *wm);

#endif
