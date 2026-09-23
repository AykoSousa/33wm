// wm.h
#ifndef WM_H
#define WM_H

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include "layout.h"

#define NUM_WORKSPACES 9

typedef struct {
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t root;
    xcb_key_symbols_t *keysyms;

    Workspace workspaces[NUM_WORKSPACES];
    int active_ws_idx;
    Workspace *ws;

    xcb_atom_t atom_wm_protocols;
    xcb_atom_t atom_wm_delete_window;

    xcb_atom_t atom_net_supported;
    xcb_atom_t atom_net_number_of_desktops;
    xcb_atom_t atom_net_current_desktop;
    xcb_atom_t atom_net_supporting_wm_check;
    xcb_atom_t atom_net_wm_name;
    xcb_atom_t atom_net_wm_window_type;
    xcb_atom_t atom_net_wm_window_type_dock;
    xcb_window_t wm_check_win;
} WM;

WM*  wm_init(void);
void wm_cleanup(WM *wm);
void wm_apply_layout(WM *wm);
void wm_run(WM *wm);
void wm_close_active_window(WM *wm);

void wm_switch_workspace(WM *wm, int target_idx);
void wm_move_active_col_to_workspace(WM *wm, int target_idx);
void spawn(const char *cmd);

void ewmh_init(WM *wm);
void ewmh_update_current_desktop(WM *wm);
void wm_toggle_fullscreen(WM *wm);

#endif