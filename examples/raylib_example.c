#include "../src/tbe.h"


int tbe_rl_events(void) {
    int e = 0;
    static int erase_timer, goleft_timer, goright_timer;
    if (IsKeyDown(KEY_BACKSPACE)) erase_timer++; else erase_timer = 0;
    if (IsKeyDown(KEY_LEFT)) goleft_timer++; else goleft_timer = 0;
    if (IsKeyDown(KEY_RIGHT)) goright_timer++; else goright_timer = 0;

    if (    IsKeyPressed(KEY_BACKSPACE) ||
            erase_timer > 30 && 
            erase_timer % 3)
        e = TBE_ACTION_ERASE;

    if (IsKeyPressed(KEY_LEFT) && IsKeyDown(KEY_LEFT_CONTROL))
        e = TBE_ACTION_JUMP_LINE_START;
    else if (IsKeyPressed(KEY_RIGHT) && IsKeyDown(KEY_LEFT_CONTROL))
        e = TBE_ACTION_JUMP_LINE_END;
    
    else if (IsKeyPressed(KEY_UP) && IsKeyDown(KEY_LEFT_CONTROL))
        e = TBE_ACTION_JUMP_TEXT_START;
    else if (IsKeyPressed(KEY_DOWN) && IsKeyDown(KEY_LEFT_CONTROL))
        e = TBE_ACTION_JUMP_TEXT_END;

    else if (IsKeyPressed(KEY_LEFT) || goleft_timer > 30)
        e = TBE_ACTION_LEFT;
    else if (IsKeyPressed(KEY_RIGHT) || goright_timer > 30)
        e = TBE_ACTION_RIGHT;

    else if (IsKeyPressed(KEY_UP))
        e = TBE_ACTION_UP;
    else if (IsKeyPressed(KEY_DOWN))
        e = TBE_ACTION_DOWN;

    else if (IsKeyPressed(KEY_ENTER) && IsKeyDown(KEY_LEFT_CONTROL))
        e = TBE_ACTION_BREAKLINE;
    return e;
}

tbe_Context prompt = {0};

void frame(void) {
    //const int ev[2] = {GetCharPressed(),0};
    const int cp = GetCharPressed();
    int ret_count = 0;
    const char* ch = CodepointToUTF8(cp, &ret_count);
    tbe_edit(&prompt, tbe_rl_events(), ch);
    //DrawText(prompt.as_string, 10, 10, 48*1, RAYWHITE);
   
    const char* l = tbe_cursor_left_text(&prompt);
    const char* r = tbe_cursor_right_text(&prompt);

    const char* line = 0;
    for(int i = 0; tbe_get_line(&prompt, &line); i++) {
        DrawText(TextFormat("%s",line), 10, 0 + 48*i + 10, 48*1, RAYWHITE);
    }


    DrawText("Left and right from cursor:", 10, GetScreenHeight() - 48*3 - 10, 48*1, RAYWHITE);
    DrawText(l, 10, GetScreenHeight() - 48*2 - 10, 48*1, RAYWHITE);
    DrawText(r, 10, GetScreenHeight() - 48 - 10, 48*1, RAYWHITE);

    //DrawText(l, 10, 10 + 48 * 2, 48, RAYWHITE);
    //DrawText(r, 10, 10 + 48 * 3, 48, RAYWHITE);

    /*
    float pos_x = 
        MeasureText(prompt.as_string, 48) * 
        ((float)prompt.built_string.count / (float)prompt.cursor_x)
    ;
    */

    enum {x,y};
    const int* c = tbe_cursor(&prompt);

    float pos_x = 
        MeasureText(l, 48);
    float pos_y = c[y] * 48;
    Rectangle cursor = {
        10 + pos_x + 2,
        10 + pos_y,
        4,
        48
    };

    
    DrawRectangleRec(cursor, RAYWHITE);
}


#if 0
int main(void) {
    tbe_Buffer n1 = {0};
    tbe_Buffer n2 = {0};
    tbe_Buffer n3 = {0};
    tbe_Buffer n4 = {0};

    tbe_buffer_append(&n1, "Hello");
    tbe_buffer_append(&n2, "Wonderful");
    tbe_buffer_append(&n3, "World");
    tbe_buffer_append(&n4, "Insertion");

    tbe_Line l1 = {0}, l2 = {0}, 
             l3 = {0}, l4 = {0};
    l1.content = n1;
    l2.content = n2;
    l3.content = n3;
    l4.content = n4;

    tbe_line_insert_after(&l1, &l2);
    tbe_line_insert_after(&l2, &l3);
    tbe_line_insert_after(&l1, &l4);

    tbe_Line* removed = tbe_line_remove_after(&l1);

    tbe_Line* iter = &l1;
    while(iter) {
        printf("'%.*s'\n", (int)iter->content.count, iter->content.items);
        iter = iter->next;
    }


    tbe_buffer_free(&(removed->content));
    tbe_buffer_free(&l1.content);
    tbe_buffer_free(&l2.content);
    tbe_buffer_free(&l3.content);
    //tbe_buffer_clear(&d);
}
#endif

#if 1
int main(void) {
    SetTargetFPS(60);
    InitWindow(800,600,"window");
    while(!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(GetColor(0x131313FF));
        frame();
        EndDrawing();
    }
    CloseWindow();
    tbe_free(&prompt);
}
#endif
