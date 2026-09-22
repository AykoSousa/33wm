#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "layout.h"

void test_dynamic_insertion_and_removal() {
    Workspace ws = {
        .first_col = NULL,
        .active_col = NULL,
        .camera_x = 0,
        .screen_w = 1920,
        .screen_h = 1080,
        .gap = 0
    };

    Window w1 = {.id = 1}, w2 = {.id = 2}, w3 = {.id = 3};
    Column col1 = {.windows = &w1, .window_count = 1, .width = 500};
    Column col2 = {.windows = &w2, .window_count = 1, .width = 500};
    Column col3 = {.windows = &w3, .window_count = 1, .width = 500};

    workspace_insert_column_after(&ws, &col1, NULL);
    assert(ws.first_col == &col1);
    assert(ws.active_col == &col1);
    assert(w1.x == 0);

    workspace_insert_column_after(&ws, &col2, &col1);
    assert(col1.next == &col2);
    assert(col2.prev == &col1);
    assert(ws.active_col == &col2);

    workspace_insert_column_after(&ws, &col3, &col1);
    assert(col1.next == &col3);
    assert(col3.prev == &col1);
    assert(col3.next == &col2);
    assert(col2.prev == &col3);
    assert(ws.active_col == &col3);

    workspace_remove_column(&ws, &col3);
    assert(col1.next == &col2);
    assert(col2.prev == &col1);
    assert(ws.active_col == &col2);

    workspace_remove_column(&ws, &col1);
    assert(ws.first_col == &col2);
    assert(col2.prev == NULL);

    workspace_remove_column(&ws, &col2);
    assert(ws.first_col == NULL);
    assert(ws.active_col == NULL);
    assert(ws.camera_x == 0);

    printf("✔ test_dynamic_insertion_and_removal passed!\n");
}

int main() {
    test_dynamic_insertion_and_removal();
    return 0;
}