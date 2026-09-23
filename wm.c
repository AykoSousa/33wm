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
    grab_key(wm, XK_v, MOD); // Super + V (Consumir coluna)
    grab_key(wm, XK_v, MOD | XCB_MOD_MASK_SHIFT); // Super + Shift + V (Expulsar janela)
    grab_key(wm, XK_j, MOD); // Super + J (Foco abaixo)
    grab_key(wm, XK_k, MOD); // Super + K (Foco acima)
    grab_key(wm, XK_minus, MOD); // Super + - (Diminuir largura)
    grab_key(wm, XK_equal, MOD); // Super + = (Aumentar largura)
    grab_key(wm, XK_f, MOD); // Super + F (Fullscreen)

    for (int i = 0; i < NUM_WORKSPACES; i++) {
        grab_key(wm, XK_1 + i, MOD);
        grab_key(wm, XK_1 + i, MOD | XCB_MOD_MASK_SHIFT);
    }

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

// Inicializa o suporte a EWMH anunciando as capacidades do WM
void ewmh_init(WM *wm) {
    wm->atom_net_supported           = get_atom(wm->conn, "_NET_SUPPORTED");
    wm->atom_net_number_of_desktops  = get_atom(wm->conn, "_NET_NUMBER_OF_DESKTOPS");
    wm->atom_net_current_desktop     = get_atom(wm->conn, "_NET_CURRENT_DESKTOP");
    wm->atom_net_supporting_wm_check = get_atom(wm->conn, "_NET_SUPPORTING_WM_CHECK");
    wm->atom_net_wm_name             = get_atom(wm->conn, "_NET_WM_NAME");
    wm->atom_net_wm_window_type      = get_atom(wm->conn, "_NET_WM_WINDOW_TYPE");
    wm->atom_net_wm_window_type_dock = get_atom(wm->conn, "_NET_WM_WINDOW_TYPE_DOCK");

    // 1. Cria uma janela invisível necessária pelo padrão EWMH (_NET_SUPPORTING_WM_CHECK)
    wm->wm_check_win = xcb_generate_id(wm->conn);
    xcb_create_window(wm->conn, XCB_COPY_FROM_PARENT, wm->wm_check_win, wm->root,
                      -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                      XCB_COPY_FROM_PARENT, 0, NULL);

    // Define o nome do nosso WM
    const char *wm_name = "Niri-X11";
    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->wm_check_win,
                        wm->atom_net_wm_name, get_atom(wm->conn, "UTF8_STRING"), 8,
                        strlen(wm_name), wm_name);

    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->wm_check_win,
                        wm->atom_net_supporting_wm_check, XCB_ATOM_WINDOW, 32,
                        1, &wm->wm_check_win);

    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->root,
                        wm->atom_net_supporting_wm_check, XCB_ATOM_WINDOW, 32,
                        1, &wm->wm_check_win);

    // 2. Anuncia o número total de desktops (9)
    uint32_t num_desktops = NUM_WORKSPACES;
    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->root,
                        wm->atom_net_number_of_desktops, XCB_ATOM_CARDINAL, 32,
                        1, &num_desktops);

    // 3. Anuncia quais átomos de suporte estão ativos
    xcb_atom_t net_atoms[] = {
        wm->atom_net_supported,
        wm->atom_net_number_of_desktops,
        wm->atom_net_current_desktop,
        wm->atom_net_supporting_wm_check
    };

    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->root,
                        wm->atom_net_supported, XCB_ATOM_ATOM, 32,
                        sizeof(net_atoms) / sizeof(xcb_atom_t), net_atoms);

    // 4. Define o desktop inicial (0)
    ewmh_update_current_desktop(wm);
}

// Atualiza o índice da área de trabalho ativa no X11
void ewmh_update_current_desktop(WM *wm) {
    uint32_t current = wm->active_ws_idx;
    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->root,
                        wm->atom_net_current_desktop, XCB_ATOM_CARDINAL, 32,
                        1, &current);
    xcb_flush(wm->conn);
}

WM* wm_init(void) {
    WM *wm = malloc(sizeof(WM));

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

    // Inicializa os 9 workspaces virtuais
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        wm->workspaces[i].first_col = NULL;
        wm->workspaces[i].active_col = NULL;
        wm->workspaces[i].camera_x = 0;
        wm->workspaces[i].screen_w = wm->screen->width_in_pixels;
        wm->workspaces[i].screen_h = wm->screen->height_in_pixels;
        wm->workspaces[i].gap = 12;
    }
    wm->active_ws_idx = 0;
    wm->ws = &wm->workspaces[0];

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
    ewmh_init(wm);
    return wm;
}

void wm_apply_layout(WM *wm) {
    Workspace *ws = wm->ws;
    if (!ws) return;

    workspace_recalculate(ws);

    // Identifica a janela ativa atual (se existir)
    uint32_t active_win_id = 0;
    if (ws->active_col && ws->active_col->window_count > 0) {
        active_win_id = ws->active_col->windows[ws->active_col->active_win_idx].id;
    }

    for (Column *col = ws->first_col; col != NULL; col = col->next) {
        for (int i = 0; i < col->window_count; i++) {
            uint32_t win_id = col->windows[i].id;

            if (ws->is_fullscreen) {
                if (win_id == active_win_id) {
                    // Aplica tamanho em tela cheia e exibe
                    uint32_t values[] = {
                        col->windows[i].x,
                        col->windows[i].y,
                        col->windows[i].w,
                        col->windows[i].h
                    };
                    xcb_configure_window(wm->conn, win_id,
                        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                        XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                        values);
                    xcb_map_window(wm->conn, win_id);
                } else {
                    // Oculta janelas em segundo plano no modo fullscreen
                    xcb_unmap_window(wm->conn, win_id);
                }
            } else {
                // Aplica posição/tamanho normal do mosaico
                uint32_t values[] = {
                    col->windows[i].x,
                    col->windows[i].y,
                    col->windows[i].w,
                    col->windows[i].h
                };
                xcb_configure_window(wm->conn, win_id,
                    XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                    XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                    values);
                xcb_map_window(wm->conn, win_id);
            }
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
        target = (xcb_window_t)col->windows[col->active_win_idx].id;
    }

    xcb_set_input_focus(wm->conn, XCB_INPUT_FOCUS_POINTER_ROOT, target, XCB_CURRENT_TIME);
    xcb_flush(wm->conn);
}

void wm_toggle_fullscreen(WM *wm) {
    if (!wm || !wm->ws || !wm->ws->active_col) return;

    // Alterna o estado (0 para 1, ou 1 para 0)
    wm->ws->is_fullscreen = !wm->ws->is_fullscreen;

    wm_apply_layout(wm);
    wm_focus_active(wm);
}

static void handle_key_press(WM *wm, xcb_key_press_event_t *ev) {
    uint16_t state = ev->state & ~LOCK_MASKS;
    xcb_keysym_t keysym = xcb_key_symbols_get_keysym(wm->keysyms, ev->detail, 0);

    // Super + H (Esquerda)
    if (keysym == XK_h && state == MOD) {
        if (wm->ws->active_col && wm->ws->active_col->prev) {
            workspace_focus_column(wm->ws, wm->ws->active_col->prev);
            wm_apply_layout(wm);
            wm_focus_active(wm);
        }
    }
    // Super + L (Direita)
    else if (keysym == XK_l && state == MOD) {
        if (wm->ws->active_col && wm->ws->active_col->next) {
            workspace_focus_column(wm->ws, wm->ws->active_col->next);
            wm_apply_layout(wm);
            wm_focus_active(wm);
        }
    }
    // Super + J (Abaixo)
    else if (keysym == XK_j && state == MOD) {
        if (wm->ws->active_col) {
            column_focus_next_window(wm->ws->active_col);
            wm_apply_layout(wm);
            wm_focus_active(wm);
        }
    }
    // Super + K (Acima)
    else if (keysym == XK_k && state == MOD) {
        if (wm->ws->active_col) {
            column_focus_prev_window(wm->ws->active_col);
            wm_apply_layout(wm);
            wm_focus_active(wm);
        }
    }
    // Super + V (Consumir coluna da direita)
    else if (keysym == XK_v && state == MOD) {
        workspace_consume_next_column(wm->ws);
        wm_apply_layout(wm);
        wm_focus_active(wm);
    }
    // Super + Shift + V (Expulsar janela para nova coluna)
    else if (keysym == XK_v && state == (MOD | XCB_MOD_MASK_SHIFT)) {
        workspace_expel_active_window(wm->ws);
        wm_apply_layout(wm);
        wm_focus_active(wm);
    }
    else if (keysym == XK_Return && state == MOD) {
        spawn(TERMINAL);
    }
    else if (keysym == XK_q && state == MOD) {
        wm_close_active_window(wm);
    }
    // Super + Minus (-) -> Diminui a largura da coluna em 50px
    else if (keysym == XK_minus && state == MOD) {
        workspace_resize_active_column(wm->ws, -50);
        wm_apply_layout(wm);
        wm_focus_active(wm);
    }
    // Super + Equal (= / +) -> Aumenta a largura da coluna em 50px
    else if ((keysym == XK_equal || keysym == XK_plus) && 
            (state == MOD || state == (MOD | XCB_MOD_MASK_SHIFT))) {
        workspace_resize_active_column(wm->ws, 50);
        wm_apply_layout(wm);
        wm_focus_active(wm);
    }
    
    if (keysym == XK_f && state == MOD) {
        wm_toggle_fullscreen(wm);
    }
    
    if (keysym >= XK_1 && keysym <= XK_9) {
        int target_ws = keysym - XK_1;

        if (state == MOD) {
            wm_switch_workspace(wm, target_ws);
        } 
        // Super + Shift + 1..9 -> Mover coluna ativa para o Workspace destino
        else if (state == (MOD | XCB_MOD_MASK_SHIFT)) {
            wm_move_active_col_to_workspace(wm, target_ws);
        }
    }
}

// Retorna true se a janela anuncia _NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DOCK.
// É assim que barras como o lemonbar/polybar se identificam - a maioria delas
// NÃO usa override_redirect, então checar só esse flag não é suficiente.
static bool is_dock_window(WM *wm, xcb_window_t win) {
    if (wm->atom_net_wm_window_type == XCB_NONE) return false;

    xcb_get_property_cookie_t cookie = xcb_get_property(
        wm->conn, 0, win, wm->atom_net_wm_window_type,
        XCB_ATOM_ATOM, 0, 32
    );
    xcb_get_property_reply_t *reply = xcb_get_property_reply(wm->conn, cookie, NULL);
    if (!reply) return false;

    bool is_dock = false;
    if (reply->type == XCB_ATOM_ATOM && xcb_get_property_value_length(reply) > 0) {
        xcb_atom_t *types = (xcb_atom_t *)xcb_get_property_value(reply);
        int count = xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t);
        for (int i = 0; i < count; i++) {
            if (types[i] == wm->atom_net_wm_window_type_dock) {
                is_dock = true;
                break;
            }
        }
    }
    free(reply);
    return is_dock;
}

static void handle_map_request(WM *wm, xcb_map_request_event_t *ev) {
    // Consulta os atributos da janela que solicitou ser exibida
    xcb_get_window_attributes_cookie_t cookie = xcb_get_window_attributes(wm->conn, ev->window);
    xcb_get_window_attributes_reply_t *attr = xcb_get_window_attributes_reply(wm->conn, cookie, NULL);
    bool override_redirect = attr && attr->override_redirect;
    if (attr) free(attr);

    // Barras, menus e popups (override_redirect OU _NET_WM_WINDOW_TYPE_DOCK):
    // apenas mapeia no X11, sem tocar em posição/tamanho e sem entrar no layout.
    // A própria barra já pediu a geometria dela (ex: lemonbar com -g).
    if (override_redirect || is_dock_window(wm, ev->window)) {
        xcb_map_window(wm->conn, ev->window);
        xcb_flush(wm->conn);
        return;
    }

    // Janela comum (ex: terminal, navegador): adiciona ao layout
    workspace_add_window(wm->ws, ev->window);
    xcb_map_window(wm->conn, ev->window);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

static void handle_destroy_notify(WM *wm, xcb_destroy_notify_event_t *ev) {
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        workspace_remove_window_by_id(&wm->workspaces[i], ev->window);
    }
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

/* O servidor avisa quando o mapa de teclado muda (o Xephyr faz isso ao
 * sincronizar com o teclado do host). Os keycodes grabbados ficam velhos. */
static void handle_mapping_notify(WM *wm, xcb_mapping_notify_event_t *ev) {
    if (ev->request == XCB_MAPPING_KEYBOARD || ev->request == XCB_MAPPING_MODIFIER) {
        xcb_refresh_keyboard_mapping(wm->keysyms, ev);
        grab_keys(wm);
    }
}

static void handle_client_message(WM *wm, xcb_client_message_event_t *ev) {
    if (ev->type == wm->atom_net_current_desktop) {
        uint32_t target_ws = ev->data.data32[0];
        if (target_ws < NUM_WORKSPACES) {
            wm_switch_workspace(wm, target_ws);
        }
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
            case XCB_CLIENT_MESSAGE:
                handle_client_message(wm, (xcb_client_message_event_t *)ev);
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

    Column *col = wm->ws->active_col;
    xcb_window_t win = (xcb_window_t)col->windows[col->active_win_idx].id;

    xcb_client_message_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = win;
    ev.type = wm->atom_wm_protocols;
    ev.format = 32;
    ev.data.data32[0] = wm->atom_wm_delete_window;
    ev.data.data32[1] = XCB_CURRENT_TIME;

    xcb_send_event(wm->conn, 0, win, XCB_EVENT_MASK_NO_EVENT, (const char *)&ev);
    xcb_flush(wm->conn);
}

// Alterna a exibição para a Workspace de índice target_idx (0 a 8)
void wm_switch_workspace(WM *wm, int target_idx) {
    if (target_idx < 0 || target_idx >= NUM_WORKSPACES || target_idx == wm->active_ws_idx) return;

    Workspace *old_ws = &wm->workspaces[wm->active_ws_idx];
    for (Column *col = old_ws->first_col; col != NULL; col = col->next) {
        for (int i = 0; i < col->window_count; i++) {
            xcb_unmap_window(wm->conn, (xcb_window_t)col->windows[i].id);
        }
    }

    wm->active_ws_idx = target_idx;
    ewmh_update_current_desktop(wm);
    wm->ws = &wm->workspaces[target_idx];

    Workspace *new_ws = wm->ws;
    for (Column *col = new_ws->first_col; col != NULL; col = col->next) {
        for (int i = 0; i < col->window_count; i++) {
            xcb_map_window(wm->conn, (xcb_window_t)col->windows[i].id);
        }
    }

    wm_apply_layout(wm);
    wm_focus_active(wm);
}

// Super + Shift + 1..9: Move a coluna ativa para outra Workspace
void wm_move_active_col_to_workspace(WM *wm, int target_idx) {
    if (target_idx < 0 || target_idx >= NUM_WORKSPACES || target_idx == wm->active_ws_idx) return;
    if (!wm->ws->active_col) return;

    Column *col = wm->ws->active_col;
    Workspace *old_ws = wm->ws;
    Workspace *new_ws = &wm->workspaces[target_idx];

    workspace_remove_column(old_ws, col);

    for (int i = 0; i < col->window_count; i++) {
        xcb_unmap_window(wm->conn, (xcb_window_t)col->windows[i].id);
    }

    workspace_insert_column_after(new_ws, col, new_ws->active_col);

    wm_apply_layout(wm);
    wm_focus_active(wm);
}