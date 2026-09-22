#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/keysym.h>
#include "wm.h"

#define MOD XCB_MOD_MASK_4
#define TERMINAL "konsole"   /* or "xterm", "alacritty", "kitty", "st" */

#define BORDER_WIDTH 3
#define COLOR_ACTIVE 0x7aa2f7  // Azul / Violeta
#define COLOR_INACTIVE 0x3b4252  // Cinza escuro

/* Modificadores de "lock" que não devem influenciar os atalhos:
 * CapsLock = Lock, NumLock = Mod2 (na prática, em praticamente todo layout). */
#define LOCK_MASKS (XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2)

void spawn(const char *cmd) {
    if (fork() == 0) {
        setsid();
        char *args[] = {"/bin/sh", "-c", (char *)cmd, NULL};
        execvp(args[0], args);
        perror("Error to execute command via execvp");
        exit(EXIT_FAILURE);
    }
}

/* Faz o grab da tecla para todas as combinações de CapsLock/NumLock.
 * Sem isso, com NumLock ou CapsLock ligado o evento chega com state
 * = MOD|Mod2 e o servidor não casa com o grab feito só com MOD. */
static void grab_key(WM *wm, xcb_keysym_t keysym, uint16_t modifier) {
    xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(wm->keysyms, keysym);
    if (!keycodes) {
        fprintf(stderr, "[WM] warning: keysym 0x%x without keycode in the current keyboard\n", keysym);
        return;
    }

    const uint16_t extra[4] = {
        0,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2,
        XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2
    };

    for (int i = 0; keycodes[i] != XCB_NO_SYMBOL; i++) {
        for (int j = 0; j < 4; j++) {
            xcb_void_cookie_t ck = xcb_grab_key_checked(
                wm->conn,
                1,
                wm->root,
                modifier | extra[j],
                keycodes[i],
                XCB_GRAB_MODE_ASYNC,
                XCB_GRAB_MODE_ASYNC
            );
            xcb_generic_error_t *err = xcb_request_check(wm->conn, ck);
            if (err) {
                /* BadAccess (10) = outro cliente já tem esse atalho grabbado */
                fprintf(stderr, "[WM] warning: grab failed (keysym 0x%x, keycode %d, error %d)\n",
                        keysym, keycodes[i], err->error_code);
                free(err);
            }
        }
    }
    free(keycodes);
}

static void grab_keys(WM *wm) {
    /* Limpa grabs antigos (útil ao recarregar o mapa de teclado) */
    xcb_ungrab_key(wm->conn, XCB_GRAB_ANY, wm->root, XCB_MOD_MASK_ANY);

    grab_key(wm, XK_h, MOD);  // Super + H (Left focus)
    grab_key(wm, XK_l, MOD);  // Super + L (Right focus)
    grab_key(wm, XK_Return, MOD);  // Super + Enter (Terminal)
    grab_key(wm, XK_q, MOD); // Super + Q (Close Window)

    xcb_flush(wm->conn);
}

static xcb_atom_t get_atom(xcb_connection_t *conn, const char *name) {
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
    if (!reply) return XCB_NONE;
    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

WM* wm_init(Workspace *ws) {
    WM *wm = malloc(sizeof(WM));
    wm->ws = ws;

    wm->conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(wm->conn)) {
        fprintf(stderr, "Erro ao conectar ao servidor X11\n");
        free(wm);
        return NULL;
    }

    const xcb_setup_t *setup = xcb_get_setup(wm->conn);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    wm->screen = iter.data;
    wm->root = wm->screen->root;

    ws->screen_w = wm->screen->width_in_pixels;
    ws->screen_h = wm->screen->height_in_pixels;

    wm->keysyms = xcb_key_symbols_alloc(wm->conn);

    wm->atom_wm_protocols = get_atom(wm->conn, "WM_PROTOCOLS");
    wm->atom_wm_delete_window = get_atom(wm->conn, "WM_DELETE_WINDOW");

    uint32_t values[1] = {
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
    };

    xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(
        wm->conn, wm->root, XCB_CW_EVENT_MASK, values
    );

    xcb_generic_error_t *error = xcb_request_check(wm->conn, cookie);
    if (error != NULL) {
        fprintf(stderr, "Another Window Manager already running.\n");
        free(error);
        xcb_key_symbols_free(wm->keysyms);
        xcb_disconnect(wm->conn);
        free(wm);
        return NULL;
    }

    grab_keys(wm);

    return wm;
}

void wm_apply_layout(WM *wm) {
    workspace_recalculate(wm->ws);

    Column *active_col = wm->ws->active_col;

    for (Column *col = wm->ws->first_col; col != NULL; col = col->next) {
        for (int i = 0; i < col->window_count; i++) {
            Window *w = &col->windows[i];
            xcb_window_t win_id = (xcb_window_t)w->id;

            // Define se a janela pertence à coluna ativa
            uint32_t border_color = (col == active_col) ? COLOR_ACTIVE : COLOR_INACTIVE;

            // 1. Aplica a cor da borda
            xcb_change_window_attributes(
                wm->conn,
                win_id,
                XCB_CW_BORDER_PIXEL,
                &border_color
            );

            // 2. Aplica posição, tamanho e largura da borda
            uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                            XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                            XCB_CONFIG_WINDOW_BORDER_WIDTH;

            uint32_t values[5] = {
                (uint32_t)w->x,
                (uint32_t)w->y,
                (uint32_t)w->w,
                (uint32_t)w->h,
                BORDER_WIDTH
            };

            xcb_configure_window(wm->conn, win_id, mask, values);
        }
    }
    xcb_flush(wm->conn);
}

/* Dá o foco de teclado à janela da coluna ativa (ou de volta à root se
 * não houver nenhuma). Chamado sempre que a coluna ativa muda. */
static void wm_focus_active(WM *wm) {
    Column *col = wm->ws->active_col;
    xcb_window_t target = wm->root;

    if (col && col->window_count > 0) {
        target = (xcb_window_t)col->windows[0].id;
    }

    xcb_set_input_focus(wm->conn, XCB_INPUT_FOCUS_POINTER_ROOT, target, XCB_CURRENT_TIME);
    xcb_flush(wm->conn);
}

static void handle_key_press(WM *wm, xcb_key_press_event_t *ev) {
    /* Ignora CapsLock/NumLock; compara exato para que Super+Shift+H
     * não dispare o atalho de Super+H. */
    uint16_t state = ev->state & ~LOCK_MASKS;
    if (state != MOD) return;

    xcb_keysym_t keysym = xcb_key_symbols_get_keysym(wm->keysyms, ev->detail, 0);

    if (keysym == XK_h) {
        if (wm->ws->active_col && wm->ws->active_col->prev) {
            workspace_focus_column(wm->ws, wm->ws->active_col->prev);
            wm_apply_layout(wm);
            wm_focus_active(wm);
        }
    }
    else if (keysym == XK_l) {
        if (wm->ws->active_col && wm->ws->active_col->next) {
            workspace_focus_column(wm->ws, wm->ws->active_col->next);
            wm_apply_layout(wm);
            wm_focus_active(wm);
        }
    }
    else if (keysym == XK_Return) {
        spawn(TERMINAL);
    }
    else if (keysym == XK_q) {
        wm_close_active_window(wm);
    }
}

static void handle_map_request(WM *wm, xcb_map_request_event_t *ev) {
    Window *new_win = calloc(1, sizeof(Window));
    new_win->id = ev->window;

    Column *new_col = calloc(1, sizeof(Column));   // calloc: next/prev começam em NULL
    new_col->windows = new_win;
    new_col->window_count = 1;
    new_col->width = wm->ws->screen_w / 2;

    workspace_insert_column_after(wm->ws, new_col, wm->ws->active_col);

    // Mapeia a janela no X11
    xcb_map_window(wm->conn, ev->window);

    wm_apply_layout(wm);
    wm_focus_active(wm);
}

static void handle_destroy_notify(WM *wm, xcb_destroy_notify_event_t *ev) {
    for (Column *col = wm->ws->first_col; col != NULL; col = col->next) {
        for (int i = 0; i < col->window_count; i++) {
            if (col->windows[i].id == ev->window) {
                workspace_remove_column(wm->ws, col);
                free(col->windows);
                free(col);
                wm_apply_layout(wm);
                wm_focus_active(wm);
                return;
            }
        }
    }
}

/* O servidor avisa quando o mapa de teclado muda (o Xephyr faz isso ao
 * sincronizar com o teclado do host). Os keycodes grabbados ficam velhos. */
static void handle_mapping_notify(WM *wm, xcb_mapping_notify_event_t *ev) {
    if (ev->request == XCB_MAPPING_KEYBOARD || ev->request == XCB_MAPPING_MODIFIER) {
        xcb_refresh_keyboard_mapping(wm->keysyms, ev);
        grab_keys(wm);
    }
}

void wm_run(WM *wm) {
    xcb_generic_event_t *ev;

    while ((ev = xcb_wait_for_event(wm->conn))) {
        uint8_t response_type = ev->response_type & ~0x80;

        switch (response_type) {
            case XCB_MAP_REQUEST:
                handle_map_request(wm, (xcb_map_request_event_t *)ev);
                break;
            case XCB_DESTROY_NOTIFY:
                handle_destroy_notify(wm, (xcb_destroy_notify_event_t *)ev);
                break;
            case XCB_KEY_PRESS:
                handle_key_press(wm, (xcb_key_press_event_t *)ev);
                break;
            case XCB_MAPPING_NOTIFY:
                handle_mapping_notify(wm, (xcb_mapping_notify_event_t *)ev);
                break;
            default:
                break;
        }
        free(ev);
    }
}

void wm_cleanup(WM *wm) {
    if (!wm) return;
    if (wm->keysyms) {
        xcb_key_symbols_free(wm->keysyms);
    }
    if (wm->conn) {
        xcb_disconnect(wm->conn);
    }
    free(wm);
}

void wm_close_active_window(WM *wm) {
    if (!wm->ws->active_col || wm->ws->active_col->window_count == 0) return;

    xcb_window_t win = (xcb_window_t)wm->ws->active_col->windows[0].id;

    xcb_client_message_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = win;
    ev.type = wm->atom_wm_protocols;
    ev.format = 32;
    ev.data.data32[0] = wm->atom_wm_delete_window;
    ev.data.data32[1] = XCB_CURRENT_TIME;

    xcb_send_event(
        wm->conn,
        0,
        win,
        XCB_EVENT_MASK_NO_EVENT,
        (const char *)&ev
    );

    xcb_flush(wm->conn);
}