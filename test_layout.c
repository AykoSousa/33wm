#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "layout.h"

void test_single_column_positioning() {
    Window win1 = {.id = 101};
    Column col1 = {
        .windows = &win1, 
        .window_count = 1, 
        .width = 400, 
        .next = NULL, 
        .prev = NULL
    };
    
    Workspace ws = {
        .first_col = &col1,
        .active_col = &col1,
        .camera_x = 0,
        .screen_w = 1920,
        .screen_h = 1080,
        .gap = 10
    };

    workspace_recalculate(&ws, 0);

    assert(win1.x == 10);
    assert(win1.y == 10);
    assert(win1.w == 380);
    assert(win1.h == 1060);
    
    printf("✔ test_single_column_positioning passed!\n");
}

int main() {
    test_single_column_positioning();
    return 0;
}