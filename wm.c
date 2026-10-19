#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <xcb/randr.h>
#include "wm.h"

/* Onde o `make install` põe a barra (usada quando ela não está ao lado do executável) */
#ifndef BAR_SCRIPT
#define BAR_SCRIPT "/usr/local/share/33wm/33bar"
#endif

/* Modificadores de "lock" que não devem influenciar os atalhos:
 * CapsLock = Lock, NumLock = Mod2 (na prática, em praticamente todo layout). */
#define LOCK_MASKS (XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2)

/* Teclas de mídia: sem modificador, cada uma roda o comando media_cmd[] configurado */
static const struct {
    xcb_keysym_t sym;
    int media;
} media_binds[] = {
    { XF86XK_AudioRaiseVolume,  MEDIA_VOLUME_UP },
    { XF86XK_AudioLowerVolume,  MEDIA_VOLUME_DOWN },
    { XF86XK_AudioMute,         MEDIA_VOLUME_MUTE },
    { XF86XK_AudioMicMute,      MEDIA_MIC_MUTE },
    { XF86XK_MonBrightnessUp,   MEDIA_BRIGHTNESS_UP },
    { XF86XK_MonBrightnessDown, MEDIA_BRIGHTNESS_DOWN },
    { XF86XK_AudioPlay,         MEDIA_PLAY },
    { XF86XK_AudioNext,         MEDIA_NEXT },
    { XF86XK_AudioPrev,         MEDIA_PREV },
};
#define NUM_MEDIA_BINDS (sizeof(media_binds) / sizeof(media_binds[0]))

pid_t spawn(WM *wm, const char *cmd) {
    if (!cmd || strlen(cmd) == 0) return 0;

    pid_t pid = fork();
    if (pid == 0) {
        // No processo filho: fecha o fd da conexão XCB do WM
        if (wm && wm->conn) {
            close(xcb_get_file_descriptor(wm->conn));
        }
        // Cria uma nova sessão para desvincular do WM (e o grupo serve para encerrar a barra)
        setsid();

        // O WM ignora SIGPIPE; os programas filhos têm que receber o padrão (ex.: um pipeline
        // que deve morrer quando o programa da ponta fecha)
        signal(SIGPIPE, SIG_DFL);

        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);

        // Se falhar o execl
        perror("spawn failed");
        _exit(1);
    }
    /* O grupo do filho precisa existir já, senão um kill(-pid) logo em seguida (ex.: a
     * barra reiniciada no evento RandR inicial) não acha ninguém. O setsid do filho pode
     * falhar depois disso, mas o pgid fica igual ao pid de qualquer forma. */
    if (pid > 0) setpgid(pid, pid);
    return pid;
}

/* Faz o grab da tecla para todas as combinações de CapsLock/NumLock.
 * Sem isso, com NumLock ou CapsLock ligado o evento chega com state
 * = wm->mod|Mod2 e o servidor não casa com o grab feito só com wm->mod. */
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
    /* Limpa grabs antigos (útil ao recarregar o mapa de teclado ou a configuração) */
    xcb_ungrab_key(wm->conn, XCB_GRAB_ANY, wm->root, XCB_MOD_MASK_ANY);

    grab_key(wm, XK_h, wm->mod);  // Super + H (Left focus)
    grab_key(wm, XK_l, wm->mod);  // Super + L (Right focus)
    grab_key(wm, XK_Return, wm->mod);  // Super + Enter (Terminal)
    grab_key(wm, XK_q, wm->mod); // Super + Q (Close Window)
    grab_key(wm, XK_v, wm->mod); // Super + V (Consumir coluna)
    grab_key(wm, XK_v, wm->mod | XCB_MOD_MASK_SHIFT); // Super + Shift + V (Expulsar janela)
    grab_key(wm, XK_j, wm->mod); // Super + J (Foco abaixo)
    grab_key(wm, XK_k, wm->mod); // Super + K (Foco acima)
    grab_key(wm, XK_Left, wm->mod);  // Super + seta esquerda (mesmo que H)
    grab_key(wm, XK_Right, wm->mod); // Super + seta direita (mesmo que L)
    grab_key(wm, XK_Down, wm->mod);  // Super + seta abaixo (mesmo que J)
    grab_key(wm, XK_Up, wm->mod);    // Super + seta acima (mesmo que K)
    grab_key(wm, XK_minus, wm->mod); // Super + - (Diminuir largura)
    grab_key(wm, XK_equal, wm->mod); // Super + = (Aumentar largura)
    grab_key(wm, XK_f, wm->mod); // Super + F (Fullscreen)
    grab_key(wm, XK_d, wm->mod); // Super + D
    grab_key(wm, XK_space, wm->mod);
    grab_key(wm, XK_m, wm->mod);
    grab_key(wm, XK_comma, wm->mod);                         // Super + , (monitor anterior)
    grab_key(wm, XK_period, wm->mod);                        // Super + . (próximo monitor)
    grab_key(wm, XK_comma, wm->mod | XCB_MOD_MASK_SHIFT);    // Super + Shift + , (coluna ao monitor anterior)
    grab_key(wm, XK_period, wm->mod | XCB_MOD_MASK_SHIFT);   // Super + Shift + . (coluna ao próximo monitor)

    for (int i = 0; i < NUM_WORKSPACES; i++) {
        grab_key(wm, XK_1 + i, wm->mod);
        grab_key(wm, XK_1 + i, wm->mod | XCB_MOD_MASK_SHIFT);
    }

    for (size_t i = 0; i < NUM_MEDIA_BINDS; i++) {
        grab_key(wm, media_binds[i].sym, 0);
    }

    xcb_flush(wm->conn);
}

/* Botões do mouse com Super: botão 1 move e botão 3 redimensiona janelas flutuantes.
 * O grab fica no root, então vale para qualquer janela (a press não chega ao app).
 * A máscara inclui motion e release: durante o arrasto o grab passivo vira implícito e
 * entrega esses eventos ao WM, que assim não precisa de xcb_grab_pointer. */
static void grab_buttons(WM *wm) {
    xcb_ungrab_button(wm->conn, XCB_BUTTON_INDEX_ANY, wm->root, XCB_MOD_MASK_ANY);

    const uint16_t extra[4] = {
        0,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2,
        XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2
    };
    const xcb_button_index_t buttons[2] = { XCB_BUTTON_INDEX_1, XCB_BUTTON_INDEX_3 };
    const uint16_t mask = XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                          XCB_EVENT_MASK_POINTER_MOTION;

    for (int b = 0; b < 2; b++) {
        for (int j = 0; j < 4; j++) {
            xcb_grab_button(wm->conn, 0, wm->root, mask,
                            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                            wm->root, XCB_NONE, buttons[b], wm->mod | extra[j]);
        }
    }
    xcb_flush(wm->conn);
}

/* Pinta o fundo do root com a cor de root_bg. Sem isso, o servidor mostra o que sobrou
 * na tela (ex.: a tela de login do SDDM). */
static void apply_root_background(WM *wm) {
    uint32_t bg = wm->config.root_bg;
    xcb_change_window_attributes(wm->conn, wm->root, XCB_CW_BACK_PIXEL, &bg);
    xcb_clear_area(wm->conn, 0, wm->root, 0, 0, 0, 0);
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
    wm->atom_net_wm_window_type_dialog  = get_atom(wm->conn, "_NET_WM_WINDOW_TYPE_DIALOG");
    wm->atom_net_wm_window_type_utility = get_atom(wm->conn, "_NET_WM_WINDOW_TYPE_UTILITY");
    wm->atom_net_wm_window_type_splash  = get_atom(wm->conn, "_NET_WM_WINDOW_TYPE_SPLASH");
    wm->atom_net_wm_window_type_toolbar = get_atom(wm->conn, "_NET_WM_WINDOW_TYPE_TOOLBAR");
    wm->atom_occupied_ws             = get_atom(wm->conn, "_33WM_OCCUPIED");
    wm->atom_net_wm_state            = get_atom(wm->conn, "_NET_WM_STATE");
    wm->atom_net_wm_state_fullscreen = get_atom(wm->conn, "_NET_WM_STATE_FULLSCREEN");
    wm->atom_net_active_window       = get_atom(wm->conn, "_NET_ACTIVE_WINDOW");
    wm->atom_net_client_list         = get_atom(wm->conn, "_NET_CLIENT_LIST");

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
        wm->atom_net_supporting_wm_check,
        wm->atom_net_wm_state,
        wm->atom_net_wm_state_fullscreen,
        wm->atom_net_active_window,
        wm->atom_net_client_list
    };

    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->root,
                        wm->atom_net_supported, XCB_ATOM_ATOM, 32,
                        sizeof(net_atoms) / sizeof(xcb_atom_t), net_atoms);

    // 4. Define o desktop inicial (o do monitor focado)
    ewmh_update_current_desktop(wm);
    ewmh_update_occupied(wm);
}

// Atualiza o índice da área de trabalho ativa no X11
void ewmh_update_current_desktop(WM *wm) {
    uint32_t current = wm->active_ws_idx;
    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->root,
                        wm->atom_net_current_desktop, XCB_ATOM_CARDINAL, 32,
                        1, &current);
    xcb_flush(wm->conn);
}

// Publica no root uma máscara com os workspaces que têm janelas (bit i = workspace i).
// A barra usa isso para mostrar só os workspaces ocupados e o ativo.
void ewmh_update_occupied(WM *wm) {
    uint32_t mask = 0;
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        for (Column *col = wm->workspaces[i].first_col; col != NULL; col = col->next) {
            if (col->window_count > 0) {
                mask |= 1u << i;
                break;
            }
        }
    }
    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->root,
                        wm->atom_occupied_ws, XCB_ATOM_CARDINAL, 32,
                        1, &mask);
}

/* ---------- Monitores ---------- */

/* Detecta os monitores (RandR 1.5). Sem RandR, usa a tela inteira como um monitor.
 * Ordem: primário primeiro, depois da esquerda para a direita. Os workspaces já atribuídos
 * a monitores que continuam existindo são mantidos; monitores novos recebem os livres. */
static void detect_monitors(WM *wm) {
    typedef struct { int x, y, w, h; bool primary; } Info;
    Info info[MAX_MONITORS];
    int n = 0;

    xcb_randr_query_version_reply_t *ver = xcb_randr_query_version_reply(
        wm->conn, xcb_randr_query_version(wm->conn, 1, 5), NULL);
    if (ver) {
        free(ver);
        xcb_randr_get_monitors_reply_t *r = xcb_randr_get_monitors_reply(
            wm->conn, xcb_randr_get_monitors(wm->conn, wm->root, 1), NULL);
        if (r) {
            xcb_randr_monitor_info_iterator_t it = xcb_randr_get_monitors_monitors_iterator(r);
            for (; it.rem && n < MAX_MONITORS; xcb_randr_monitor_info_next(&it)) {
                info[n].x = it.data->x;
                info[n].y = it.data->y;
                info[n].w = it.data->width;
                info[n].h = it.data->height;
                info[n].primary = it.data->primary != 0;
                n++;
            }
            free(r);
        }
    }
    if (n == 0) {
        info[0].x = 0;
        info[0].y = 0;
        info[0].w = wm->screen->width_in_pixels;
        info[0].h = wm->screen->height_in_pixels;
        info[0].primary = true;
        n = 1;
    }

    /* Ordenação simples: primário primeiro, depois por x */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            bool swap = (!info[i].primary && info[j].primary) ||
                        (info[i].primary == info[j].primary && info[j].x < info[i].x);
            if (swap) {
                Info tmp = info[i];
                info[i] = info[j];
                info[j] = tmp;
            }
        }
    }

    /* Workspace de cada monitor: o que ele já tinha, ou o menor livre */
    int ws_of[MAX_MONITORS];
    for (int i = 0; i < n; i++) {
        ws_of[i] = (i < wm->monitor_count) ? wm->monitors[i].ws : -1;
    }
    for (int i = 0; i < n; i++) {
        if (ws_of[i] >= 0) continue;
        for (int k = 0; k < NUM_WORKSPACES; k++) {
            bool used = false;
            for (int j = 0; j < n; j++) {
                if (ws_of[j] == k) used = true;
            }
            if (!used) {
                ws_of[i] = k;
                break;
            }
        }
        if (ws_of[i] < 0) ws_of[i] = 0; /* mais monitores que workspaces: compartilha o 0 */
    }

    for (int i = 0; i < n; i++) {
        wm->monitors[i].x = info[i].x;
        wm->monitors[i].y = info[i].y;
        wm->monitors[i].w = info[i].w;
        wm->monitors[i].h = info[i].h;
        wm->monitors[i].ws = ws_of[i];
    }
    wm->monitor_count = n;
    if (wm->focused_mon >= n) wm->focused_mon = 0;
}

// Índice do monitor que mostra o workspace ws, ou -1 se ele estiver escondido
static int monitor_showing(const WM *wm, int ws) {
    for (int m = 0; m < wm->monitor_count; m++) {
        if (wm->monitors[m].ws == ws) return m;
    }
    return -1;
}

// Índice do monitor que contém o ponto (x, y) da tela, ou -1 se nenhum
static int monitor_at(const WM *wm, int x, int y) {
    for (int m = 0; m < wm->monitor_count; m++) {
        const Monitor *mon = &wm->monitors[m];
        if (x >= mon->x && x < mon->x + mon->w && y >= mon->y && y < mon->y + mon->h) return m;
    }
    return -1;
}

// Índice do monitor onde a janela está (pelo centro dela), ou -1 se nenhum
static int monitor_of_window(WM *wm, xcb_window_t win) {
    xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(wm->conn, xcb_get_geometry(wm->conn, win), NULL);
    if (!g) return -1;
    int m = monitor_at(wm, g->x + g->width / 2, g->y + g->height / 2);
    free(g);
    return m;
}

// Foca o monitor m: o workspace dele passa a ser o ativo (teclas, criação de janelas)
static void set_focus_monitor(WM *wm, int m) {
    wm->focused_mon = m;
    wm->active_ws_idx = wm->monitors[m].ws;
    wm->ws = &wm->workspaces[wm->active_ws_idx];
    ewmh_update_current_desktop(wm);
}

/* ---------- Janelas gerenciadas ---------- */

static void set_border_color(WM *wm, xcb_window_t win, bool focused) {
    uint32_t color = focused ? wm->config.border_active : wm->config.border_inactive;
    xcb_change_window_attributes(wm->conn, win, XCB_CW_BORDER_PIXEL, &color);
}

/* Mostra/esconde uma janela gerenciada (coluna ou flutuante). Cada unmap pedido pelo WM
 * incrementa pending_unmaps; o UnmapNotify correspondente o consome em handle_unmap_notify.
 * Assim só um unmap sem pendência é tomado como o cliente se retirando. */
static void managed_show(WM *wm, xcb_window_t id, bool *mapped) {
    if (*mapped) return;
    *mapped = true;
    xcb_map_window(wm->conn, id);
}

static void managed_hide(WM *wm, xcb_window_t id, bool *mapped, int *pending_unmaps) {
    if (!*mapped) return;
    *mapped = false;
    (*pending_unmaps)++;
    xcb_unmap_window(wm->conn, id);
}

/* Lê WM_NORMAL_HINTS (ICCCM) de uma janela: tamanho mínimo e incrementos de redimensionamento
 * (ex.: terminais, que só mudam de tamanho em células). Campos ausentes ficam em 0. */
static void get_size_hints(WM *wm, xcb_window_t win, int *min_w, int *min_h,
                           int *inc_w, int *inc_h, int *base_w, int *base_h) {
    *min_w = *min_h = *inc_w = *inc_h = *base_w = *base_h = 0;

    xcb_get_property_cookie_t cookie = xcb_get_property(
        wm->conn, 0, win, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 0, 18
    );
    xcb_get_property_reply_t *reply = xcb_get_property_reply(wm->conn, cookie, NULL);
    if (reply && reply->type == XCB_ATOM_WM_SIZE_HINTS && xcb_get_property_value_length(reply) >= 18 * 4) {
        /* WM_SIZE_HINTS: flags, x, y, w, h, min_w, min_h, max_w, max_h, inc_w, inc_h,
         * min_aspect(2), max_aspect(2), base_w, base_h, win_gravity */
        const uint32_t *f = (const uint32_t *)xcb_get_property_value(reply);
        uint32_t flags = f[0];
        if (flags & (1u << 4)) { *min_w = (int)f[5];  *min_h = (int)f[6]; }   /* PMinSize */
        if (flags & (1u << 6)) { *inc_w = (int)f[9];  *inc_h = (int)f[10]; }  /* PResizeInc */
        if (flags & (1u << 8)) { *base_w = (int)f[15]; *base_h = (int)f[16]; } /* PBaseSize */
    }
    free(reply);
}

/* Posiciona a janela no slot do layout. A borda fica por dentro do slot, como no i3:
 * o cliente é encolhido pela largura da borda, então o layout não muda.
 * Em fullscreen a borda some e os hints não se aplicam. */
static void place_window(WM *wm, const Window *w, bool fullscreen, bool focused) {
    int bw = fullscreen ? 0 : wm->config.border_width;
    int inner_w = w->w - 2 * bw;
    int inner_h = w->h - 2 * bw;

    if (!fullscreen) {
        int min_w, min_h, inc_w, inc_h, base_w, base_h;
        get_size_hints(wm, w->id, &min_w, &min_h, &inc_w, &inc_h, &base_w, &base_h);

        // Respeita o tamanho mínimo (pode passar do slot, como no i3) e arredonda para baixo
        // nos incrementos do app, a partir do tamanho base
        if (inc_w > 1 && inner_w > base_w) inner_w = base_w + ((inner_w - base_w) / inc_w) * inc_w;
        if (inc_h > 1 && inner_h > base_h) inner_h = base_h + ((inner_h - base_h) / inc_h) * inc_h;
        if (inner_w < min_w) inner_w = min_w;
        if (inner_h < min_h) inner_h = min_h;
    }
    if (inner_w < 1) inner_w = 1;
    if (inner_h < 1) inner_h = 1;

    // No X a borda fica por fora de (x, y): desloca pela borda para a moldura caber no slot
    uint32_t values[] = { (uint32_t)(w->x + bw), (uint32_t)(w->y + bw), (uint32_t)inner_w, (uint32_t)inner_h, (uint32_t)bw };
    xcb_configure_window(wm->conn, w->id,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                         XCB_CONFIG_WINDOW_BORDER_WIDTH, values);

    set_border_color(wm, w->id, focused);
}

/* Anuncia (ou retira) _NET_WM_STATE_FULLSCREEN na janela, para o app saber o estado real */
static void ewmh_set_fullscreen_state(WM *wm, xcb_window_t win, bool fullscreen) {
    if (fullscreen) {
        xcb_atom_t state = wm->atom_net_wm_state_fullscreen;
        xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, win, wm->atom_net_wm_state,
                            XCB_ATOM_ATOM, 32, 1, &state);
    } else {
        xcb_delete_property(wm->conn, win, wm->atom_net_wm_state);
    }
}

/* Publica _NET_CLIENT_LIST no root: todas as janelas gerenciadas (colunas de todos os
 * workspaces e flutuantes), para barras e ferramentas */
static void ewmh_update_clients(WM *wm) {
    int count = 0;
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        for (Column *col = wm->workspaces[i].first_col; col != NULL; col = col->next) {
            count += col->window_count;
        }
    }
    for (FloatWin *f = wm->floats; f != NULL; f = f->next) count++;

    xcb_window_t *list = malloc(sizeof(xcb_window_t) * (count > 0 ? count : 1));
    if (!list) return;

    int n = 0;
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        for (Column *col = wm->workspaces[i].first_col; col != NULL; col = col->next) {
            for (int j = 0; j < col->window_count; j++) list[n++] = col->windows[j].id;
        }
    }
    for (FloatWin *f = wm->floats; f != NULL; f = f->next) list[n++] = f->id;

    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->root, wm->atom_net_client_list,
                        XCB_ATOM_WINDOW, 32, n, list);
    free(list);
}

/* Aplica o layout de um workspace. mon == NULL: o workspace está escondido e todas as suas
 * janelas são desmapeadas. Caso contrário, o layout (relativo ao monitor) é desenhado no
 * monitor mon, que tem índice mon_idx. Só o monitor principal (0) reserva o topo para a barra. */
static void apply_workspace(WM *wm, int idx, const Monitor *mon, int mon_idx) {
    Workspace *ws = &wm->workspaces[idx];

    if (!mon) {
        for (Column *col = ws->first_col; col != NULL; col = col->next) {
            for (int i = 0; i < col->window_count; i++) {
                Window *w = &col->windows[i];
                ewmh_set_fullscreen_state(wm, w->id, false);
                managed_hide(wm, w->id, &w->mapped, &w->pending_unmaps);
            }
        }
        return;
    }

    int top = (mon_idx == 0) ? wm->config.top_padding : 0;
    ws->screen_w = mon->w;
    ws->screen_h = mon->h;
    workspace_recalculate(ws, top);

    bool is_focused_ws = (idx == wm->active_ws_idx);

    uint32_t active_win_id = 0;
    if (ws->active_col && ws->active_col->window_count > 0) {
        active_win_id = ws->active_col->windows[ws->active_col->active_win_idx].id;
    }

    for (Column *col = ws->first_col; col != NULL; col = col->next) {
        for (int i = 0; i < col->window_count; i++) {
            Window *w = &col->windows[i];
            bool is_active = (w->id == active_win_id);
            bool focused = is_focused_ws && is_active && wm->focus_float == 0;

            // Coordenadas do layout são relativas ao monitor: desloca para a tela
            Window pos = *w;
            pos.x += mon->x;
            pos.y += mon->y;

            ewmh_set_fullscreen_state(wm, w->id, ws->is_fullscreen && is_active);

            if (ws->is_fullscreen) {
                if (is_active) {
                    place_window(wm, &pos, true, true);
                    managed_show(wm, w->id, &w->mapped);
                } else {
                    managed_hide(wm, w->id, &w->mapped, &w->pending_unmaps);
                }
            } else if (col->is_maximized) {
                // Se a coluna estiver maximizada, oculta as janelas que não são a ativa
                if (i == col->active_win_idx) {
                    place_window(wm, &pos, false, focused);
                    managed_show(wm, w->id, &w->mapped);
                } else {
                    managed_hide(wm, w->id, &w->mapped, &w->pending_unmaps);
                }
            } else {
                // Modo normal de mosaico. Com mais de um monitor, uma coluna cortada pela borda do
                // monitor apareceria por cima do vizinho (o X não recorta pela borda de um monitor
                // interno), então ela fica escondida até caber inteira. A ativa é centralizada
                // pela câmera e sempre aparece.
                bool inside = pos.x >= mon->x && pos.x + pos.w <= mon->x + mon->w;
                if (wm->monitor_count > 1 && !inside && !is_active) {
                    managed_hide(wm, w->id, &w->mapped, &w->pending_unmaps);
                } else {
                    place_window(wm, &pos, false, focused);
                    managed_show(wm, w->id, &w->mapped);
                }
            }
        }
    }
}

/* Redesenha tudo: cada workspace é aplicado no monitor que o mostra (ou escondido), e as
 * janelas flutuantes seguem o workspace delas. Chamado sempre que o layout muda. */
void wm_apply_layout(WM *wm) {
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        int m = monitor_showing(wm, i);
        apply_workspace(wm, i, m >= 0 ? &wm->monitors[m] : NULL, m);
    }

    for (FloatWin *f = wm->floats; f != NULL; f = f->next) {
        if (monitor_showing(wm, f->ws) >= 0) {
            managed_show(wm, f->id, &f->mapped);
        } else {
            managed_hide(wm, f->id, &f->mapped, &f->pending_unmaps);
        }
    }

    ewmh_update_occupied(wm);
    ewmh_update_clients(wm);
    xcb_flush(wm->conn);
}

/* Atualiza as cores de borda do workspace ativo: só a janela com foco fica com a cor ativa.
 * A janela de foco é a flutuante, se houver; senão, a da coluna ativa. */
static void refresh_focus_borders(WM *wm) {
    Column *col = wm->ws->active_col;
    if (col && col->window_count > 0) {
        xcb_window_t win = col->windows[col->active_win_idx].id;
        set_border_color(wm, win, wm->focus_float == 0);
    }
    for (FloatWin *f = wm->floats; f != NULL; f = f->next) {
        if (f->ws == wm->active_ws_idx) {
            set_border_color(wm, f->id, f->id == wm->focus_float);
        }
    }
}

/* Dá o foco de teclado à janela flutuante com foco, ou à janela da coluna ativa
 * (ou de volta à root se não houver nenhuma). Chamado sempre que o foco muda. */
void wm_focus_active(WM *wm) {
    if (!wm || !wm->ws) return;

    xcb_window_t target = wm->focus_float;
    if (!target && wm->ws->active_col && wm->ws->active_col->window_count > 0) {
        Column *col = wm->ws->active_col;
        target = col->windows[col->active_win_idx].id;
    }

    refresh_focus_borders(wm);

    // _NET_ACTIVE_WINDOW: a janela com foco (0 = nenhuma), para barras e ferramentas
    xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->root, wm->atom_net_active_window,
                        XCB_ATOM_WINDOW, 32, 1, &target);

    if (!target) {
        xcb_flush(wm->conn);
        return;
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

void wm_toggle_column_maximize(WM *wm) {
    if (!wm || !wm->ws || !wm->ws->active_col || wm->ws->is_fullscreen) return;

    column_toggle_maximize(wm->ws, wm->ws->active_col);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

/* Em fullscreen o workspace continua em fullscreen: a janela que ganha o foco passa a ocupar
 * a tela, pois apply_workspace mostra só a ativa. */
void wm_focus_prev_col(WM *wm) {
    if (!wm || !wm->ws) return;

    workspace_focus_prev_column(wm->ws);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

void wm_focus_next_col(WM *wm) {
    if (!wm || !wm->ws) return;

    workspace_focus_next_column(wm->ws);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

void wm_focus_prev_win(WM *wm) {
    if (!wm || !wm->ws || !wm->ws->active_col) return;

    column_focus_prev_window(wm->ws->active_col);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

void wm_focus_next_win(WM *wm) {
    if (!wm || !wm->ws || !wm->ws->active_col) return;

    column_focus_next_window(wm->ws->active_col);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

/* Super + , / . : foca o monitor anterior / seguinte */
void wm_focus_monitor(WM *wm, int delta) {
    if (wm->monitor_count < 2) return;
    int m = (wm->focused_mon + delta + wm->monitor_count) % wm->monitor_count;

    wm->focus_float = 0;
    set_focus_monitor(wm, m);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

/* Super + Shift + , / . : manda a coluna ativa para o workspace do monitor anterior / seguinte */
void wm_move_col_to_monitor(WM *wm, int delta) {
    if (wm->monitor_count < 2) return;
    int m = (wm->focused_mon + delta + wm->monitor_count) % wm->monitor_count;
    wm_move_active_col_to_workspace(wm, wm->monitors[m].ws);
}

static void dispatch_key(WM *wm, xcb_keysym_t keysym, uint16_t state) {
    // Super + H ou seta esquerda (Left)
    if ((keysym == XK_h || keysym == XK_Left) && state == wm->mod) {
        wm_focus_prev_col(wm);
    }
    // Super + L ou seta direita (Right)
    else if ((keysym == XK_l || keysym == XK_Right) && state == wm->mod) {
        wm_focus_next_col(wm);
    }
    // Super + J ou seta abaixo (Below)
    else if ((keysym == XK_j || keysym == XK_Down) && state == wm->mod) {
        wm_focus_next_win(wm);
    }
    // Super + K ou seta acima (Above)
    else if ((keysym == XK_k || keysym == XK_Up) && state == wm->mod) {
        wm_focus_prev_win(wm);
    }
    // Super + V (Catch right column)
    else if (keysym == XK_v && state == wm->mod) {
        workspace_consume_next_column(wm->ws);
        wm_apply_layout(wm);
        wm_focus_active(wm);
    }
    // Super + Shift + V (Expulsar janela para nova coluna)
    else if (keysym == XK_v && state == (wm->mod | XCB_MOD_MASK_SHIFT)) {
        workspace_expel_active_window(wm->ws);
        wm_apply_layout(wm);
        wm_focus_active(wm);
    }
    else if (keysym == XK_Return && state == wm->mod) {
        spawn(wm, wm->config.terminal_cmd);
    }
    else if (keysym == XK_q && state == wm->mod) {
        wm_close_active_window(wm);
    }
    // Super + Minus (-) -> Diminui a largura da coluna pelo resize_step
    else if (keysym == XK_minus && state == wm->mod) {
        workspace_resize_active_column(wm->ws, -wm->config.resize_step);
        wm_apply_layout(wm);
        wm_focus_active(wm);
    }
    // Super + Equal (= / +) -> Aumenta a largura da coluna pelo resize_step
    else if ((keysym == XK_equal || keysym == XK_plus) &&
            (state == wm->mod || state == (wm->mod | XCB_MOD_MASK_SHIFT))) {
        workspace_resize_active_column(wm->ws, wm->config.resize_step);
        wm_apply_layout(wm);
        wm_focus_active(wm);
    }
    else if (keysym == XK_f && state == wm->mod) {
        wm_toggle_fullscreen(wm);
    }
    else if (keysym == XK_m && state == wm->mod) {
        wm_toggle_column_maximize(wm); // Super + M -> Maximiza janela na coluna
    }
    else if (keysym == XK_comma && state == wm->mod) {
        wm_focus_monitor(wm, -1);
    }
    else if (keysym == XK_period && state == wm->mod) {
        wm_focus_monitor(wm, 1);
    }
    else if (keysym == XK_comma && state == (wm->mod | XCB_MOD_MASK_SHIFT)) {
        wm_move_col_to_monitor(wm, -1);
    }
    else if (keysym == XK_period && state == (wm->mod | XCB_MOD_MASK_SHIFT)) {
        wm_move_col_to_monitor(wm, 1);
    }
    else if (keysym >= XK_1 && keysym <= XK_9) {
        int target_ws = keysym - XK_1;

        if (state == wm->mod) {
            wm_switch_workspace(wm, target_ws);
        }
        // Super + Shift + 1..9 -> Mover coluna ativa para o Workspace destino
        else if (state == (wm->mod | XCB_MOD_MASK_SHIFT)) {
            wm_move_active_col_to_workspace(wm, target_ws);
        }
    } else if (keysym == XK_space && state == wm->mod) {
        spawn(wm, wm->config.launcher_cmd);
    }
}

static void handle_key_press(WM *wm, xcb_key_press_event_t *ev) {
    uint16_t state = ev->state & ~LOCK_MASKS;
    xcb_keysym_t keysym = xcb_key_symbols_get_keysym(wm->keysyms, ev->detail, 0);

    // Teclas de mídia só rodam o comando configurado e não mexem no foco
    if (state == 0) {
        for (size_t i = 0; i < NUM_MEDIA_BINDS; i++) {
            if (keysym == media_binds[i].sym) {
                spawn(wm, wm->config.media_cmd[media_binds[i].media]);
                return;
            }
        }
    }

    // Os atalhos de layout devolvem o foco às colunas. Fechar, lançar e o launcher
    // mantêm o foco na janela flutuante (ex.: Super+Q fecha o diálogo).
    bool keeps_float_focus = keysym == XK_q || keysym == XK_Return ||
                             keysym == XK_space || keysym == XK_d;
    xcb_window_t prev_focus = wm->focus_float;
    if (!keeps_float_focus) wm->focus_float = 0;

    dispatch_key(wm, keysym, state);

    if (prev_focus != 0 && wm->focus_float == 0) wm_focus_active(wm);
}

// Retorna true se a janela anuncia o tipo _NET_WM_WINDOW_TYPE = type.
static bool window_has_type(WM *wm, xcb_window_t win, xcb_atom_t type) {
    if (wm->atom_net_wm_window_type == XCB_NONE || type == XCB_NONE) return false;

    xcb_get_property_cookie_t cookie = xcb_get_property(
        wm->conn, 0, win, wm->atom_net_wm_window_type,
        XCB_ATOM_ATOM, 0, 32
    );
    xcb_get_property_reply_t *reply = xcb_get_property_reply(wm->conn, cookie, NULL);
    if (!reply) return false;

    bool found = false;
    if (reply->type == XCB_ATOM_ATOM && xcb_get_property_value_length(reply) > 0) {
        xcb_atom_t *types = (xcb_atom_t *)xcb_get_property_value(reply);
        int count = xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t);
        for (int i = 0; i < count; i++) {
            if (types[i] == type) {
                found = true;
                break;
            }
        }
    }
    free(reply);
    return found;
}

// Barras (lemonbar/polybar) se identificam por _NET_WM_WINDOW_TYPE_DOCK. A maioria
// NÃO usa override_redirect, então checar só esse flag não é suficiente.
static bool is_dock_window(WM *wm, xcb_window_t win) {
    return window_has_type(wm, win, wm->atom_net_wm_window_type_dock);
}

// Diálogos, utilitários, splash e toolbars, ou qualquer janela com WM_TRANSIENT_FOR
// (ex.: "salvar como" de um app), não devem ser tiled: viram janelas flutuantes.
static bool should_float(WM *wm, xcb_window_t win) {
    if (window_has_type(wm, win, wm->atom_net_wm_window_type_dialog) ||
        window_has_type(wm, win, wm->atom_net_wm_window_type_utility) ||
        window_has_type(wm, win, wm->atom_net_wm_window_type_splash) ||
        window_has_type(wm, win, wm->atom_net_wm_window_type_toolbar)) {
        return true;
    }

    xcb_get_property_cookie_t cookie = xcb_get_property(
        wm->conn, 0, win, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 0, 1
    );
    xcb_get_property_reply_t *reply = xcb_get_property_reply(wm->conn, cookie, NULL);
    bool transient = reply && xcb_get_property_value_length(reply) > 0;
    free(reply);
    return transient;
}

static FloatWin *float_find(WM *wm, xcb_window_t win) {
    for (FloatWin *f = wm->floats; f != NULL; f = f->next) {
        if (f->id == win) return f;
    }
    return NULL;
}

// Registra a janela flutuante no workspace ativo (ainda não mapeada)
static FloatWin *float_add(WM *wm, xcb_window_t win) {
    FloatWin *f = malloc(sizeof(FloatWin));
    if (!f) return NULL;
    f->id = win;
    f->ws = wm->active_ws_idx;
    f->mapped = false;
    f->pending_unmaps = 0;
    f->next = wm->floats;
    wm->floats = f;
    ewmh_update_clients(wm);
    return f;
}

// Remove a janela da lista de flutuantes (no-op se ela não for flutuante)
static void float_remove(WM *wm, xcb_window_t win) {
    FloatWin **link = &wm->floats;
    while (*link != NULL) {
        if ((*link)->id == win) {
            FloatWin *dead = *link;
            *link = dead->next;
            free(dead);
            if (wm->focus_float == win) wm->focus_float = 0;
            ewmh_update_clients(wm);
            return;
        }
        link = &(*link)->next;
    }
}

// Centraliza a janela flutuante na área do monitor focado (abaixo da barra, se for o
// principal), no tamanho que ela pediu. Sem tamanho útil, usa metade da área.
// Mostra a janela e dá a ela o foco.
static void float_place(WM *wm, FloatWin *f) {
    const Monitor *mon = &wm->monitors[wm->focused_mon];
    int top = (wm->focused_mon == 0) ? wm->config.top_padding : 0;

    int area_x = mon->x;
    int area_y = mon->y + top;
    int area_w = mon->w;
    int area_h = mon->h - top;

    int w = area_w / 2;
    int h = area_h / 2;
    xcb_get_geometry_reply_t *geo = xcb_get_geometry_reply(wm->conn, xcb_get_geometry(wm->conn, f->id), NULL);
    if (geo && geo->width > 1 && geo->height > 1) {
        w = geo->width + 2 * wm->config.border_width;
        h = geo->height + 2 * wm->config.border_width;
    }
    free(geo);
    if (w > area_w) w = area_w;
    if (h > area_h) h = area_h;

    Window slot = {
        .id = f->id,
        .x = area_x + (area_w - w) / 2,
        .y = area_y + (area_h - h) / 2,
        .w = w,
        .h = h,
    };
    place_window(wm, &slot, false, true);
    managed_show(wm, f->id, &f->mapped);
}

// Dá o foco à janela flutuante (ela passa a ter prioridade sobre o layout em colunas)
static void float_focus(WM *wm, xcb_window_t win) {
    wm->focus_float = win;
    wm_focus_active(wm);
}

/* Clique (botão 1 ou 3, sem modificador) numa janela gerenciada: um grab passivo na própria
 * janela entrega o clique ao WM antes do app. O clique é devolvido depois (ReplayPointer),
 * então o app o recebe normalmente. Com Super o grab do root tem prioridade e nada muda. */
static void grab_click_to_focus(WM *wm, xcb_window_t win) {
    const uint16_t extra[4] = {
        0,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2,
        XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2
    };
    const xcb_button_index_t buttons[2] = { XCB_BUTTON_INDEX_1, XCB_BUTTON_INDEX_3 };

    for (int b = 0; b < 2; b++) {
        for (int j = 0; j < 4; j++) {
            xcb_grab_button(wm->conn, 0, win, XCB_EVENT_MASK_BUTTON_PRESS,
                            XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC,
                            XCB_NONE, XCB_NONE, buttons[b], extra[j]);
        }
    }
    xcb_flush(wm->conn);
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

    // Diálogos e janelas transientes: flutuam no workspace ativo, fora do layout
    if (should_float(wm, ev->window)) {
        FloatWin *f = float_add(wm, ev->window);
        if (f) {
            float_place(wm, f);
            grab_click_to_focus(wm, ev->window);
            float_focus(wm, ev->window);
        }
        return;
    }

    // Janela comum (ex: terminal, navegador): adiciona ao layout. O layout a mostra.
    grab_click_to_focus(wm, ev->window);
    workspace_add_window(wm->ws, ev->window);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

static void handle_destroy_notify(WM *wm, xcb_destroy_notify_event_t *ev) {
    float_remove(wm, ev->window);
    if (wm->close_pending == ev->window) wm->close_pending = 0;
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        workspace_remove_window_by_id(&wm->workspaces[i], ev->window);
    }
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

// Localiza uma janela em coluna (de qualquer workspace). Os ponteiros valem até a próxima
// alteração do layout, então use logo após a busca.
static Window *find_tiled(WM *wm, xcb_window_t win, int *ws_out, Column **col_out, int *idx_out) {
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        for (Column *col = wm->workspaces[i].first_col; col != NULL; col = col->next) {
            for (int j = 0; j < col->window_count; j++) {
                if (col->windows[j].id == win) {
                    if (ws_out) *ws_out = i;
                    if (col_out) *col_out = col;
                    if (idx_out) *idx_out = j;
                    return &col->windows[j];
                }
            }
        }
    }
    return NULL;
}

/* UnmapNotify: unmaps pedidos pelo próprio WM são consumidos pelo contador pending_unmaps.
 * Qualquer outro unmap é o cliente se retirando (ex.: esconder a janela): ela sai do
 * layout, como numa destruição, e volta se for mapeada de novo (novo MapRequest). */
static void handle_unmap_notify(WM *wm, xcb_unmap_notify_event_t *ev) {
    FloatWin *f = float_find(wm, ev->window);
    if (f) {
        if (f->pending_unmaps > 0) {
            f->pending_unmaps--;
            return;
        }
        float_remove(wm, ev->window);
        wm_focus_active(wm);
        return;
    }

    Window *w = find_tiled(wm, ev->window, NULL, NULL, NULL);
    if (!w) return;
    if (w->pending_unmaps > 0) {
        w->pending_unmaps--;
        return;
    }

    for (int i = 0; i < NUM_WORKSPACES; i++) {
        workspace_remove_window_by_id(&wm->workspaces[i], ev->window);
    }
    if (wm->close_pending == ev->window) wm->close_pending = 0;
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

/* ConfigureRequest: janelas gerenciadas têm a geometria decidida pelo layout, então o app
 * recebe de volta a geometria atual (ConfigureNotify sintético, ICCCM). Janelas ainda não
 * gerenciadas (ex.: docks) têm o pedido aplicado como foi feito. */
static void handle_configure_request(WM *wm, xcb_configure_request_event_t *ev) {
    bool managed = find_tiled(wm, ev->window, NULL, NULL, NULL) != NULL ||
                   float_find(wm, ev->window) != NULL;

    if (managed) {
        xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(
            wm->conn, xcb_get_geometry(wm->conn, ev->window), NULL
        );
        if (g) {
            xcb_configure_notify_event_t n;
            memset(&n, 0, sizeof(n));
            n.response_type = XCB_CONFIGURE_NOTIFY;
            n.event = ev->window;
            n.window = ev->window;
            n.above_sibling = XCB_NONE;
            n.x = g->x;
            n.y = g->y;
            n.width = g->width;
            n.height = g->height;
            n.border_width = g->border_width;
            n.override_redirect = 0;
            xcb_send_event(wm->conn, 0, ev->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&n);
            free(g);
        }
    } else {
        // Os valores vêm na ordem dos bits de value_mask: X, Y, W, H, border, sibling, stack
        uint32_t values[7];
        uint32_t mask = ev->value_mask;
        int n = 0;
        if (mask & XCB_CONFIG_WINDOW_X)            values[n++] = (uint32_t)ev->x;
        if (mask & XCB_CONFIG_WINDOW_Y)            values[n++] = (uint32_t)ev->y;
        if (mask & XCB_CONFIG_WINDOW_WIDTH)        values[n++] = ev->width;
        if (mask & XCB_CONFIG_WINDOW_HEIGHT)       values[n++] = ev->height;
        if (mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) values[n++] = ev->border_width;
        if (mask & XCB_CONFIG_WINDOW_SIBLING)      values[n++] = ev->sibling;
        if (mask & XCB_CONFIG_WINDOW_STACK_MODE)   values[n++] = ev->stack_mode;
        xcb_configure_window(wm->conn, ev->window, mask, values);
    }
    xcb_flush(wm->conn);
}

/* _NET_WM_STATE_FULLSCREEN pedido pelo app (ex.: vídeo no navegador). Só vale para
 * colunas do workspace ativo. A janela pedida vira a ativa e o workspace entra em
 * fullscreen (o mesmo estado do Super+F). action: 0 = remover, 1 = adicionar, 2 = alternar. */
static void request_fullscreen(WM *wm, xcb_window_t win, uint32_t action) {
    int ws_idx = -1;
    Column *col = NULL;
    int idx = -1;
    if (!find_tiled(wm, win, &ws_idx, &col, &idx) || ws_idx != (int)wm->active_ws_idx) return;

    Workspace *ws = wm->ws;
    bool is_active = ws->active_col == col && col->active_win_idx == idx;
    bool currently_fs = ws->is_fullscreen && is_active;

    bool want_fs = action == 1 || (action == 2 && !currently_fs);
    if (want_fs) {
        if (!is_active) {
            col->active_win_idx = idx;
            workspace_focus_column(ws, col);
        }
        ws->is_fullscreen = 1;
    } else if (currently_fs) {
        ws->is_fullscreen = 0;
    } else {
        return;
    }

    wm_apply_layout(wm);
    wm_focus_active(wm);
}

/* Responde a um pedido de janela de mudar o próprio estado (_NET_WM_STATE) */
static void handle_wm_state_message(WM *wm, xcb_client_message_event_t *ev) {
    uint32_t action = ev->data.data32[0];
    xcb_atom_t prop1 = ev->data.data32[1];
    xcb_atom_t prop2 = ev->data.data32[2];

    if (prop1 == wm->atom_net_wm_state_fullscreen || prop2 == wm->atom_net_wm_state_fullscreen) {
        request_fullscreen(wm, ev->window, action);
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

static void handle_client_message(WM *wm, xcb_client_message_event_t *ev) {
    if (ev->type == wm->atom_net_current_desktop) {
        uint32_t target_ws = ev->data.data32[0];
        if (target_ws < NUM_WORKSPACES) {
            wm_switch_workspace(wm, target_ws);
        }
    } else if (ev->type == wm->atom_net_wm_state) {
        handle_wm_state_message(wm, ev);
    }
}

/* Foca a janela clicada: se for flutuante, ela ganha o foco; se for de coluna, a coluna e
 * a janela viram as ativas (trocando de monitor se necessário). */
static void focus_clicked_window(WM *wm, xcb_window_t win) {
    if (float_find(wm, win)) {
        // Janela flutuante num monitor que não é o focado: o foco passa para ele, como no clique
        // em coluna, para que as próximas janelas abram ali
        int m = monitor_of_window(wm, win);
        if (m >= 0 && m != wm->focused_mon) {
            set_focus_monitor(wm, m);
            wm_apply_layout(wm);
        }
        float_focus(wm, win);
        return;
    }

    int ws_idx;
    Column *col;
    int idx;
    if (!find_tiled(wm, win, &ws_idx, &col, &idx)) return;

    for (int m = 0; m < wm->monitor_count; m++) {
        if (wm->monitors[m].ws == ws_idx && m != wm->focused_mon) {
            set_focus_monitor(wm, m);
            break;
        }
    }

    wm->focus_float = 0;
    col->active_win_idx = idx;
    workspace_focus_column(wm->ws, col);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

/* Botão 1/3 sobre uma janela flutuante com Super: começa a mover/redimensionar (janelas em
 * coluna não são arrastadas). Clique sem Super numa janela gerenciada: só foca. */
static void handle_button_press(WM *wm, xcb_button_press_event_t *ev) {
    // Clique numa janela gerenciada (grab da própria janela): foca e devolve o clique ao app
    if (ev->event != wm->root) {
        focus_clicked_window(wm, ev->event);
        xcb_allow_events(wm->conn, XCB_ALLOW_REPLAY_POINTER, ev->time);
        xcb_flush(wm->conn);
        return;
    }

    // Clique no fundo de um monitor (sem janela): o foco passa para esse monitor
    if (!wm->drag.active && ev->child == 0) {
        int m = monitor_at(wm, ev->root_x, ev->root_y);
        if (m >= 0 && m != wm->focused_mon) {
            wm->focus_float = 0;
            set_focus_monitor(wm, m);
            wm_apply_layout(wm);
            wm_focus_active(wm);
        }
    }

    if (wm->drag.active || ev->child == 0) return;

    FloatWin *f = float_find(wm, ev->child);
    if (!f) return;

    xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(wm->conn, xcb_get_geometry(wm->conn, f->id), NULL);
    if (!g) return;

    wm->drag.active = true;
    wm->drag.win = f->id;
    wm->drag.button = ev->detail;
    wm->drag.start_x = ev->root_x;
    wm->drag.start_y = ev->root_y;
    wm->drag.orig_x = g->x;
    wm->drag.orig_y = g->y;
    wm->drag.orig_w = g->width;
    wm->drag.orig_h = g->height;
    free(g);

    uint32_t above = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(wm->conn, f->id, XCB_CONFIG_WINDOW_STACK_MODE, &above);
    float_focus(wm, f->id);
    xcb_flush(wm->conn);
}

static void handle_motion(WM *wm, xcb_motion_notify_event_t *ev) {
    if (!wm->drag.active) return;

    int dx = ev->root_x - wm->drag.start_x;
    int dy = ev->root_y - wm->drag.start_y;

    if (wm->drag.button == XCB_BUTTON_INDEX_1) {
        uint32_t values[2] = { (uint32_t)(wm->drag.orig_x + dx), (uint32_t)(wm->drag.orig_y + dy) };
        xcb_configure_window(wm->conn, wm->drag.win,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    } else {
        int w = wm->drag.orig_w + dx;
        int h = wm->drag.orig_h + dy;
        if (w < 50) w = 50;
        if (h < 50) h = 50;
        uint32_t values[2] = { (uint32_t)w, (uint32_t)h };
        xcb_configure_window(wm->conn, wm->drag.win,
                             XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    }
    xcb_flush(wm->conn);
}

static void handle_button_release(WM *wm, xcb_button_release_event_t *ev) {
    if (!wm->drag.active || ev->detail != wm->drag.button) return;

    wm->drag.active = false;
    xcb_flush(wm->conn);
}

/* ---------- Barra e recarga ---------- */

/* Caminho da barra: bar/33bar ao lado do executável (desenvolvimento) ou o instalado */
static void bar_script_path(char *out, size_t size) {
    char exe[512];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        exe[len] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            snprintf(out, size, "%s/bar/33bar", exe);
            if (access(out, X_OK) == 0) return;
        }
    }
    snprintf(out, size, "%s", BAR_SCRIPT);
}

/* Inicia a barra no monitor principal. A barra é um programa à parte (bar/33bar), com a
 * própria configuração: o WM só passa a posição do monitor. O laço reinicia a barra se ela
 * morrer. A altura da barra deve bater com o top_padding, o espaço que o layout reserva. */
void wm_start_bar(WM *wm) {
    char script[1024];
    bar_script_path(script, sizeof(script));

    const Monitor *mon = &wm->monitors[0];

    char cmd[1536];
    int n = snprintf(cmd, sizeof(cmd),
             "export BAR_X=%d BAR_Y=%d BAR_W=%d; "
             "while :; do '%s'; sleep 1; done",
             mon->x, mon->y, mon->w, script);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        fprintf(stderr, "[WM] bar: comando da barra longo demais\n");
        return;
    }
    wm->bar_pid = spawn(wm, cmd);
}

static void wm_restart_bar(WM *wm) {
    if (wm->bar_pid > 0) {
        kill(-wm->bar_pid, SIGTERM);
    }
    wm->bar_pid = 0;
    wm_start_bar(wm);
}

/* SIGHUP: relê o arquivo de configuração e aplica sem reiniciar o WM. As janelas
 * continuam como estavam; só mudam as opções (teclas, cores, gap...). A barra não é
 * reiniciada: ela relê o próprio arquivo (~/.config/33wm/bar). */
void wm_reload_settings(WM *wm) {
    config_free(&wm->config);
    config_load(&wm->config);
    wm->mod = strcmp(wm->config.mod, "alt") == 0 ? XCB_MOD_MASK_1 : XCB_MOD_MASK_4;

    for (int i = 0; i < NUM_WORKSPACES; i++) {
        wm->workspaces[i].gap = wm->config.gap;
        wm->workspaces[i].min_column_width = wm->config.min_column_width;
    }

    grab_keys(wm);
    grab_buttons(wm);
    apply_root_background(wm);
    wm_apply_layout(wm);
    wm_focus_active(wm);
    fprintf(stderr, "[WM] configuração recarregada\n");
}

/* Monitor conectado ou desconectado (RandR): redetecta, ajusta o foco e reinicia a barra
 * no monitor principal (que pode ter mudado). */
static void handle_screen_change(WM *wm) {
    detect_monitors(wm);
    set_focus_monitor(wm, wm->focused_mon);
    wm_restart_bar(wm);
    wm_apply_layout(wm);
    wm_focus_active(wm);
}

static void dispatch_event(WM *wm, xcb_generic_event_t *ev) {
    uint8_t response_type = ev->response_type & ~0x80;

    if (wm->randr_event && response_type == wm->randr_event) {
        handle_screen_change(wm);
        return;
    }

    switch (response_type) {
        case XCB_MAP_REQUEST:
            handle_map_request(wm, (xcb_map_request_event_t *)ev);
            break;
        case XCB_DESTROY_NOTIFY:
            handle_destroy_notify(wm, (xcb_destroy_notify_event_t *)ev);
            break;
        case XCB_UNMAP_NOTIFY:
            handle_unmap_notify(wm, (xcb_unmap_notify_event_t *)ev);
            break;
        case XCB_CONFIGURE_REQUEST:
            handle_configure_request(wm, (xcb_configure_request_event_t *)ev);
            break;
        case XCB_KEY_PRESS:
            handle_key_press(wm, (xcb_key_press_event_t *)ev);
            break;
        case XCB_BUTTON_PRESS:
            handle_button_press(wm, (xcb_button_press_event_t *)ev);
            break;
        case XCB_BUTTON_RELEASE:
            handle_button_release(wm, (xcb_button_release_event_t *)ev);
            break;
        case XCB_MOTION_NOTIFY:
            handle_motion(wm, (xcb_motion_notify_event_t *)ev);
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
}

/* Loop principal: espera no socket do X e no pipe de sinais (wake_fd). Um byte 'H' no pipe
 * pede a recarga da configuração (SIGHUP). */
void wm_run(WM *wm, int wake_fd) {
    int xfd = xcb_get_file_descriptor(wm->conn);
    struct pollfd fds[2] = {
        { .fd = xfd,     .events = POLLIN },
        { .fd = wake_fd, .events = POLLIN },
    };

    for (;;) {
        xcb_generic_event_t *ev;
        while ((ev = xcb_poll_for_event(wm->conn)) != NULL) {
            dispatch_event(wm, ev);
            free(ev);
        }
        if (xcb_connection_has_error(wm->conn)) break;
        xcb_flush(wm->conn);

        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (fds[1].revents & POLLIN) {
            char buf[16];
            ssize_t n = read(wake_fd, buf, sizeof(buf));
            for (ssize_t i = 0; i < n; i++) {
                if (buf[i] == 'H') wm_reload_settings(wm);
            }
        }
    }
}

void wm_cleanup(WM *wm) {
    if (!wm) return;
    // A barra roda em grupo próprio (setsid): encerra o grupo inteiro (supervisor, 33bar e lemonbar)
    if (wm->bar_pid > 0) {
        kill(-wm->bar_pid, SIGTERM);
    }
    if (wm->keysyms) {
        xcb_key_symbols_free(wm->keysyms);
    }
    if (wm->conn) {
        xcb_disconnect(wm->conn);
    }
    config_free(&wm->config);
    while (wm->floats) {
        FloatWin *next = wm->floats->next;
        free(wm->floats);
        wm->floats = next;
    }
    free(wm);
}

// Verdadeiro se a janela declara WM_DELETE_WINDOW em WM_PROTOCOLS
static bool window_supports_delete(WM *wm, xcb_window_t win) {
    xcb_get_property_cookie_t cookie = xcb_get_property(
        wm->conn, 0, win, wm->atom_wm_protocols, XCB_ATOM_ATOM, 0, 64
    );
    xcb_get_property_reply_t *reply = xcb_get_property_reply(wm->conn, cookie, NULL);

    bool found = false;
    if (reply && reply->type == XCB_ATOM_ATOM) {
        xcb_atom_t *protocols = (xcb_atom_t *)xcb_get_property_value(reply);
        int count = xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t);
        for (int i = 0; i < count; i++) {
            if (protocols[i] == wm->atom_wm_delete_window) {
                found = true;
                break;
            }
        }
    }
    free(reply);
    return found;
}

/* Super+Q: pede para a janela fechar via WM_DELETE_WINDOW. Se o app não suporta esse
 * protocolo, ou se a janela ainda estiver aberta no Super+Q seguinte, a conexão dela
 * é morta (xcb_kill_client). Não há timeout: o segundo Super+Q é o "forçar". */
void wm_close_active_window(WM *wm) {
    xcb_window_t win = wm->focus_float;
    if (win == 0) {
        if (!wm->ws->active_col || wm->ws->active_col->window_count == 0) return;
        Column *col = wm->ws->active_col;
        win = (xcb_window_t)col->windows[col->active_win_idx].id;
    }

    if (wm->close_pending == win || !window_supports_delete(wm, win)) {
        xcb_kill_client(wm->conn, win);
        wm->close_pending = 0;
    } else {
        xcb_client_message_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.window = win;
        ev.type = wm->atom_wm_protocols;
        ev.format = 32;
        ev.data.data32[0] = wm->atom_wm_delete_window;
        ev.data.data32[1] = XCB_CURRENT_TIME;

        xcb_send_event(wm->conn, 0, win, XCB_EVENT_MASK_NO_EVENT, (const char *)&ev);
        wm->close_pending = win;
    }
    xcb_flush(wm->conn);
}

/* Super + N: mostra o workspace target_idx. Se ele já está em outro monitor, o foco vai
 * para esse monitor; senão, ele substitui o workspace do monitor focado. */
void wm_switch_workspace(WM *wm, int target_idx) {
    if (target_idx < 0 || target_idx >= NUM_WORKSPACES || target_idx == wm->active_ws_idx) return;

    int m = monitor_showing(wm, target_idx);
    wm->focus_float = 0;

    if (m >= 0) {
        set_focus_monitor(wm, m);
    } else {
        wm->monitors[wm->focused_mon].ws = target_idx;
        set_focus_monitor(wm, wm->focused_mon);
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

    // A coluna sai deste workspace: esconde as janelas antes de entrar no novo
    for (int i = 0; i < col->window_count; i++) {
        Window *w = &col->windows[i];
        managed_hide(wm, w->id, &w->mapped, &w->pending_unmaps);
    }

    workspace_insert_column_after(new_ws, col, new_ws->active_col);

    wm_apply_layout(wm);
    wm_focus_active(wm);
}

WM* wm_init(void) {
    WM *wm = calloc(1, sizeof(WM));
    if (!wm) return NULL;

    wm->conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(wm->conn)) {
        fprintf(stderr, "Erro ao conectar ao servidor X11\n");
        free(wm);
        return NULL;
    }

    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(wm->conn));
    wm->screen = iter.data;
    wm->root = wm->screen->root;

    // Carrega a configuração antes de usar seus valores (gap, mod) nos workspaces
    config_load(&wm->config);
    wm->mod = strcmp(wm->config.mod, "alt") == 0 ? XCB_MOD_MASK_1 : XCB_MOD_MASK_4;

    /* RandR: o servidor avisa quando monitores são conectados ou removidos */
    xcb_prefetch_extension_data(wm->conn, &xcb_randr_id);
    const xcb_query_extension_reply_t *randr = xcb_get_extension_data(wm->conn, &xcb_randr_id);
    if (randr && randr->present) {
        wm->randr_event = randr->first_event + XCB_RANDR_SCREEN_CHANGE_NOTIFY;
    }

    detect_monitors(wm);

    // Inicializa os 9 workspaces virtuais. O monitor principal mostra o workspace 0.
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        wm->workspaces[i].first_col = NULL;
        wm->workspaces[i].active_col = NULL;
        wm->workspaces[i].camera_x = 0;

        wm->workspaces[i].screen_w = wm->monitors[0].w;
        wm->workspaces[i].screen_h = wm->monitors[0].h;
        wm->workspaces[i].gap = wm->config.gap;
        wm->workspaces[i].min_column_width = wm->config.min_column_width;
    }
    wm->focused_mon = 0;
    wm->active_ws_idx = wm->monitors[0].ws;
    wm->ws = &wm->workspaces[wm->active_ws_idx];

    wm->keysyms = xcb_key_symbols_alloc(wm->conn);
    wm->atom_wm_protocols = get_atom(wm->conn, "WM_PROTOCOLS");
    wm->atom_wm_delete_window = get_atom(wm->conn, "WM_DELETE_WINDOW");

    /* BUTTON_PRESS no root: clique no fundo de um monitor (sem janela) chega ao WM, que foca
     * esse monitor. Sem isso, só o clique com Super (grab) chegava. */
    uint32_t values[1] = {
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
        XCB_EVENT_MASK_BUTTON_PRESS
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

    if (wm->randr_event) {
        xcb_randr_select_input(wm->conn, wm->root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);
    }

    grab_keys(wm);
    grab_buttons(wm);
    apply_root_background(wm);
    ewmh_init(wm);
    return wm;
}
