#include <stdio.h>
#include <assert.h>
#include "layout.h"

void test_camera_centering_and_clamping() {
    Window w1 = {.id = 1}, w2 = {.id = 2}, w3 = {.id = 3};
    
    Column col3 = {.windows = &w3, .window_count = 1, .width = 600, .next = NULL};
    Column col2 = {.windows = &w2, .window_count = 1, .width = 800, .next = &col3};
    Column col1 = {.windows = &w1, .window_count = 1, .width = 400, .next = &col2};
    
    col3.prev = &col2;
    col2.prev = &col1;

    Workspace ws = {
        .first_col = &col1,
        .active_col = &col1,
        .camera_x = 0,
        .screen_w = 1920,
        .screen_h = 1080,
        .gap = 0
    };

    workspace_focus_column(&ws, &col1);
    assert(ws.camera_x == 0);
    assert(w1.x == 0);

    workspace_focus_column(&ws, &col3);
    assert(ws.camera_x == 540);
    
    assert(w3.x == 660);

    printf("✔ test_camera_centering_and_clamping passed!\n");
}

int main() {
    test_camera_centering_and_clamping();
    return 0;
}