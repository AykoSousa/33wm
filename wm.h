#ifndef WM_H
#define WM_H

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include "layout.h"

typedef struct {
    xcb_connection_t *conn;
    xcb_screen_t     *screen;
    xcb_window_t      root;
    xcb_key_symbols_t  *keysyms;
    Workspace        *ws;

    xcb_atom_t atom_wm_protocols;
    xcb_atom_t atom_wm_delete_window;
} WM;

WM*  wm_init(Workspace *ws);
void wm_cleanup(WM *wm);
void wm_apply_layout(WM *wm);
void wm_run(WM *wm);
void spawn(const char *cmd);
void wm_close_active_window(WM *wm);

#endif