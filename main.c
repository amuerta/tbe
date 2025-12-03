#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <raylib.h>

// NOW:
// TODO: fix memory leak in buffers and line list

//
// # Later when i will feel like doing this:
//
// TODO: actions: [dup line, delete line ]
// TODO: select, copy, paste


//
// Text Buffer Editor
//
//  - Small text editor in one* function call!
//
// What is TBE for?
//
//  Basically i have noticed a need for something that works similar to readline
//  family of libraries, but for graphical applications.
//
//  Technically you can use TBE in terminal apps too, but it will be a bit tricky
//  to setup due to complicated event pulling.
//
//  Goal of the library is to be incredibly easy-to-integrate text editor that
//  can be used for something like code-editors, textboxes. It should be enough
//  to just call one* function to perform edit per frame of time.
//
// How to use it?
//
//  1. You create tbe_Context (per prompt structure)
//  2. All the events you assign manually from your input source (Window events, Program events, Terminal sequence codes)
//  3. You call tbe_edit with context and input events you collected and input text
//  4. tbe_Context now contains as_string field you can just print, 
//      or you can call tbe_get_line() to iterate/drain all the lines
//  5. Optionally tbe contains a bunch of QoL functions for text wrapping and
//      measuring.
//
//  6. You display the text recieved and draw cursor.
//  7. Yay! Textbox, ready for your service!

typedef struct tbe_Context  tbe_Context;
typedef struct tbe_Line     tbe_Line;
typedef struct tbe_Buffer   tbe_Buffer;

void        tbe_edit(tbe_Context* ctx, int action, const char* input_text);
int*        tbe_cursor(tbe_Context* ctx);
const char* tbe_cursor_left_text(tbe_Context* ctx);
const char* tbe_cursor_right_text(tbe_Context* ctx);
const char* tbe_get_line(tbe_Context* ctx);
const char* tbe_get_string(tbe_Context* ctx);


typedef struct tbe_Buffer {
    char* items;
    size_t count;
    size_t capacity;
} tbe_Buffer;

typedef struct tbe_Line {
    tbe_Buffer          content;
    struct tbe_Line     *next, *prev, *tail, *head;
} tbe_Line;

typedef struct tbe_Slice {
    const char* data;
    size_t      length;
} tbe_Slice;

typedef struct tbe_Context {
    const char* as_string;

    tbe_Buffer  built_string;
    tbe_Buffer  line_temp;

    tbe_Line*   line_list;
    tbe_Line*   line_current;
    size_t      line_count;

    tbe_Buffer  cursor_left;
    tbe_Slice   cursor_right;
    size_t      cursor_x, cursor_y;
} tbe_Context;


enum {
    TBE_ACTION_BREAKLINE    = (1 << 0),
    TBE_ACTION_ERASE        = (1 << 1),
    TBE_ACTION_UP           = (1 << 2),
    TBE_ACTION_DOWN         = (1 << 3),
    TBE_ACTION_LEFT         = (1 << 4),
    TBE_ACTION_RIGHT        = (1 << 5),
    
    TBE_ACTION_JUMP_LINE_END     = (1 << 10),
    TBE_ACTION_JUMP_LINE_START   = (1 << 11),
    TBE_ACTION_JUMP_TEXT_END     = (1 << 12),
    TBE_ACTION_JUMP_TEXT_START   = (1 << 13),
    TBE_ACTION_JUMP_WORD_BACK    = (1 << 14),
    TBE_ACTION_JUMP_WORD_FORWARD = (1 << 15),
};

//
// IMPLEMENTATION
//

int tbe_clamp(int value, int min, int max) {
    if (value > max) return max;
    if (value <= min) return min;
    return value;
}

void* tbe_recalloc(void* ptr, size_t prev_size, size_t new_size) {
    assert(ptr && new_size);
    void* new_ptr = calloc(new_size, 1);
    memcpy(new_ptr, ptr, prev_size);
    free(ptr);
    return new_ptr;
}

void tbe_buffer_clear(tbe_Buffer* b) {
    memset(b->items, 0, b->capacity);
    b->count = 0;
}

void tbe_buffer_free(tbe_Buffer* b) {
    free(b->items);
    b->count = 0;
    b->capacity = 0;
}

void tbe_buffer_append_sized(tbe_Buffer* b, const char* text, size_t text_size) {
    const size_t growth_factor = 2;
    if (b->capacity == 0) {
        assert(!b->items && "Zero initlized when capacity is 0");
        b->capacity = 32; // default
        b->items = calloc(b->capacity, 1);
    }
    if (b->count + text_size > b->capacity) {
        b->capacity *= growth_factor;
        b->items = tbe_recalloc(b->items, 
                b->capacity/growth_factor, 
                b->capacity);
    }

    for(int i = 0; i < text_size; i++) 
        b->items[i + b->count] = text[i];
    b->count += text_size;
}

void tbe_buffer_append(tbe_Buffer* b, const char* text) {
    tbe_buffer_append_sized(b,text,strlen(text));
}

void tbe_buffer_concat(tbe_Buffer* dest, tbe_Buffer src) {
    
    if (!dest->capacity) {
        assert(!dest->items && "Zero initlized when capacity is 0");
        dest->capacity = src.count + 1; // default
        dest->items = calloc(dest->capacity, 1);
    }

    if (dest->count + src.count > dest->capacity) {
        size_t prev_cap = dest->capacity;
        dest->capacity = (dest->count + src.count) * 2;
        dest->items = tbe_recalloc(dest->items, 
                prev_cap, 
                dest->capacity);
    }

    memcpy(dest->items + dest->count, src.items, src.count);
    dest->count+=src.count;
}

void tbe_line_insert_after(tbe_Line *l, tbe_Line* new) {
    assert(l);
    tbe_Line *before = l->prev;
    tbe_Line *after = l->next;

    l->next = new;
    new->prev = l;

    if(after) {
        new->next = after;
        after->prev = new;
    }
}

tbe_Line* tbe_line_remove_after(tbe_Line *l) {
    assert(l);
    tbe_Line* b = l;
    tbe_Line* c = l->next;
    tbe_Line* n = 0;
    if(c && c->next) {
        n = c->next;
        b->next = n;
        n->prev = b;
        c->next = 0;
        c->prev = 0;
    } else if (c && !c->next) {
        b->next = 0;
        c->prev = 0;
    }
    return c;
}

tbe_Line* tbe_line_merge_after(tbe_Line* l) {
    assert(l);
    tbe_Line* current = l;
    tbe_Line* next = l->next;
    tbe_Line* ret = 0;
    if (next) {
        // append text
        tbe_buffer_append_sized(
                &(current->content), 
                next->content.items,
                next->content.count
        );
        // pop item from the link
        ret = tbe_line_remove_after(current);
    }
    return ret;
}


tbe_Line* tbe_list_make_node(void) {
    tbe_Line* it = 0;
    it = calloc(sizeof(*it), 1);
    return it;
}

#define tbe_flag_pop(M,F) ((M) & (~(F)))

void tbe_list_free(tbe_Line* l) {
    if(!l) return;
    tbe_list_free(l->next);
    tbe_buffer_free(&(l->content));
}

void tbe_free(tbe_Context* ctx) {
    tbe_list_free(ctx->line_list);
    tbe_buffer_free(&ctx->built_string);
    tbe_buffer_free(&ctx->cursor_left);
    tbe_buffer_free(&ctx->line_temp);
}

void tbe_edit( tbe_Context* ctx, 
        int action, 
        const char* input_text)
{

rebuild_line:
    // cleanup before running again and rebuilding
    tbe_buffer_clear(&ctx->built_string);
    tbe_buffer_clear(&ctx->cursor_left);
    tbe_buffer_clear(&ctx->line_temp);

    if (!ctx->line_current) {
        ctx->line_list = tbe_list_make_node();
        ctx->line_current = ctx->line_list;
    }

    tbe_Line* current_line = ctx->line_current;
    tbe_Buffer* buf = &(current_line->content);
    bool at_line_end = ctx->cursor_x == buf->count;
    size_t text_len = strlen(input_text);
    
    const size_t LINE_MAX_WIDTH = (1<<30);

    // build left part from cursor (useful for cursor position measurements)
    //if (text_len) {
    tbe_buffer_concat(
            &(ctx->cursor_left), 
            current_line->content
            );
    ctx->cursor_left.items[ctx->cursor_x] = 0; // Null terminate at cursor
    ctx->cursor_left.count = ctx->cursor_x; // Null terminate at cursor

    // right part too
    ctx->cursor_right = (tbe_Slice) {
        .data = current_line->content.items + ctx->cursor_x,
        .length = current_line->content.count - ctx->cursor_left.count
    };
    //}

    if (action) {
        bool go_back       = action & TBE_ACTION_LEFT ;
        bool go_forward    = action & TBE_ACTION_RIGHT;
        bool act_erase     = action & TBE_ACTION_ERASE;
        bool have_chars    = buf->count != 0;
        bool act_breakline = action & TBE_ACTION_BREAKLINE;
        bool go_down       = action & TBE_ACTION_DOWN && current_line->next;
        bool go_up         = action & TBE_ACTION_UP && current_line->prev;
        bool go_line_begin = action & TBE_ACTION_JUMP_LINE_START;
        bool go_line_end   = action & TBE_ACTION_JUMP_LINE_END;
        bool go_text_begin = action & TBE_ACTION_JUMP_TEXT_START;
        bool go_text_end   = action & TBE_ACTION_JUMP_TEXT_END;

        // move up or down
        
        if(go_line_begin) {
            ctx->cursor_x = 0;
        } 
        else if (go_line_end) {
            size_t line_len    = ctx->line_current->content.count;
            ctx->cursor_x      = tbe_clamp(line_len, 0, line_len);
        }

        else if (go_text_begin) {
            tbe_Line* iter = ctx->line_current;
            while(iter->prev) {
                iter = iter->prev;
                ctx->cursor_y--;
            }
            ctx->line_current = iter;
            ctx->cursor_x = 0;

            action = tbe_flag_pop(action, TBE_ACTION_JUMP_TEXT_START);
            goto rebuild_line;
        } 

        else if (go_text_end) {
            tbe_Line* iter = ctx->line_current;
            while(iter->next) {
                iter = iter->next;
                ctx->cursor_y++;
            }
            ctx->line_current = iter;

            size_t line_len    = ctx->line_current->content.count;
            ctx->cursor_x      = tbe_clamp(line_len, 0, line_len);

            action = tbe_flag_pop(action, TBE_ACTION_JUMP_TEXT_END);
            goto rebuild_line;
        }

        else if (go_up) {
            ctx->line_current = ctx->line_current->prev;
            size_t line_len    = ctx->line_current->content.count;
            ctx->cursor_y--;
            ctx->cursor_x = tbe_clamp(ctx->cursor_x, 0, line_len);
            action = tbe_flag_pop(action, TBE_ACTION_UP);
            goto rebuild_line;
        } 

        else if (go_down) {
            ctx->line_current = ctx->line_current->next;
            size_t line_len    = ctx->line_current->content.count;
            ctx->cursor_y++;
            ctx->cursor_x = tbe_clamp(ctx->cursor_x, 0, line_len);
            action = tbe_flag_pop(action, TBE_ACTION_DOWN);
            goto rebuild_line;
        }

        // break line
        else if (act_breakline) {
            if(!ctx->line_current->next && !buf->count) {
                ctx->line_current->next = tbe_list_make_node();
                tbe_Line* prev = ctx->line_current;
                ctx->line_current = ctx->line_current->next;
                ctx->line_current->prev = prev;
                ctx->cursor_y++;
                ctx->cursor_x = 0;
                action = tbe_flag_pop(action, TBE_ACTION_BREAKLINE);
                goto rebuild_line;
            }
            else if (ctx->line_current->next && at_line_end) {
                tbe_line_insert_after(ctx->line_current, tbe_list_make_node());
                ctx->line_current = ctx->line_current->next;
                ctx->cursor_y++;
                ctx->cursor_x = 0;//tbe_clamp(ctx->cursor_x, 0, line_len);
                action = tbe_flag_pop(action, TBE_ACTION_BREAKLINE);
                goto rebuild_line;
            } else {
                tbe_Line* after = tbe_list_make_node();
                tbe_buffer_append_sized(&(after->content), 
                        ctx->cursor_right.data, 
                        ctx->cursor_right.length);
                ctx->line_current->content.count = ctx->cursor_left.count;
                tbe_line_insert_after(ctx->line_current, after);
                ctx->line_current = after;
                ctx->cursor_y++;
                ctx->cursor_x = 0;//tbe_clamp(ctx->cursor_x, 0, line_len);
                action = tbe_flag_pop(action, TBE_ACTION_BREAKLINE);
                goto rebuild_line;
            }
        }

        // erase logic with characters existing
        else if (act_erase) {

            if (at_line_end && have_chars) {
                buf->count             = tbe_clamp(buf->count-1,    0, LINE_MAX_WIDTH);
                buf->items[buf->count] = 0;
                ctx->cursor_x          = tbe_clamp(ctx->cursor_x-1, 0, LINE_MAX_WIDTH);
            } 

            // otherwise remove one charater from left 
            // and append right.
            else if (have_chars) {
                
                if (ctx->cursor_x == 0 && ctx->line_current->prev) {
                    tbe_Line* new_curr = ctx->line_current->prev;
                    tbe_Line* remove = 0;
                    ctx->cursor_x = tbe_clamp(ctx->line_current->prev->content.count, 0, LINE_MAX_WIDTH);
                    ctx->cursor_y--;
                    remove = tbe_line_merge_after(ctx->line_current->prev);
                    ctx->line_current = new_curr;
                    free(remove);
                }

                else {
                    ctx->cursor_left.count = tbe_clamp(ctx->cursor_left.count - 1, 0, LINE_MAX_WIDTH);
                    tbe_buffer_concat(
                            &(ctx->line_temp), 
                            ctx->cursor_left
                            );
                    tbe_buffer_append_sized(
                            &(ctx->line_temp), 
                            ctx->cursor_right.data,
                            ctx->cursor_right.length
                            );
                    tbe_buffer_clear(&(current_line->content));
                    tbe_buffer_concat(&(current_line->content), ctx->line_temp);
                    ctx->cursor_x = tbe_clamp(ctx->cursor_x - 1, 0, LINE_MAX_WIDTH);
                }
            }

            else if (!have_chars) {
                if(current_line->prev) {
                    tbe_Line* move_to = ctx->line_current->prev;
                    tbe_Line* remove = tbe_line_remove_after(ctx->line_current->prev);
                    ctx->line_current = move_to;
                    ctx->cursor_y--;
                    int line_len = move_to->content.count;
                    ctx->cursor_x = tbe_clamp(line_len, 0, LINE_MAX_WIDTH);
                    free(remove);
                    //action = tbe_flag_pop(action, TBE_ACTION_ERASE);
                    //goto rebuild_line;
                } 
                
                else if (!current_line->prev && current_line->next) {
                    // TODO: free node
                    tbe_Line* old       = current_line;
                    ctx->line_current   = current_line->next;
                    ctx->line_current->prev = 0;
                    ctx->line_list = ctx->line_current;
                    // free content and node
                    tbe_buffer_free(&(old->content));
                    free(old);
                }
                // TODO: remove_head()
                /*
                else if (current_line->next) {
                    tbe_Line* move_to = ctx->line_current->next;
                    tbe_line_remove_after(ctx->line_current->prev);
                    ctx->line_current = move_to;
                    ctx->cursor_y--;
                    int line_len = move_to->content.count;
                    ctx->cursor_x = tbe_clamp(line_len, 0, LINE_MAX_WIDTH);
                }
                */
            }

        } 

        else if (go_back) {
            ctx->cursor_x = tbe_clamp(ctx->cursor_x-1, 0, LINE_MAX_WIDTH);
        }

        else if (go_forward) {
            ctx->cursor_x = tbe_clamp(ctx->cursor_x+1, 0, buf->count);
        }
    }

    // no special events that interupt input at set frame.
    // write text to the current line
    else {

        // append at the end
        if (at_line_end) {
            if (text_len) {
                tbe_buffer_append(buf, input_text);
                ctx->cursor_x += text_len;
            }
        }

        // insert after left from cursor 
        // and append right from the cursor
        // step cursor_x with text_len
        else {
            ctx->cursor_x += text_len;
            tbe_buffer_concat(
                    &(ctx->line_temp), 
                    ctx->cursor_left
            );
            tbe_buffer_append(
                    &(ctx->line_temp), 
                    input_text
            );
            tbe_buffer_append_sized(
                    &(ctx->line_temp), 
                    ctx->cursor_right.data,
                    ctx->cursor_right.length
            );
            tbe_buffer_clear(&(current_line->content));
            tbe_buffer_concat(&(current_line->content), ctx->line_temp);
        }
    }

    // build total string
    tbe_Line *line = ctx->line_list;
    while(line) {
        tbe_buffer_concat(
                &(ctx->built_string), 
                line->content //current_line->content
        );
        if (line->next)
            tbe_buffer_append(
                    &(ctx->built_string), 
                    "\n"
                    );
        line = line->next;
    }

    // update exposed C-string for printing
    ctx->as_string = ctx->built_string.items;
}

int* tbe_cursor(tbe_Context* ctx) {
    enum { x, y };
    static int position[2];
    position[x] = ctx->cursor_x;
    position[y] = ctx->cursor_y;
    return position;
}

// TODO: think whether it should build the string here
// or on each NS_edit() call.
const char* tbe_get_string(tbe_Context* ctx) {
    return ctx->as_string;
}

const char* tbe_cursor_left_text(tbe_Context* ctx) {
    return ctx->cursor_left.items;
}

const char* tbe_cursor_right_text(tbe_Context* ctx) {
    return ctx->cursor_right.data;
}

const char* tbe_get_line(tbe_Context* ctx) {
    static tbe_Line* line;
    if (!line) line = ctx->line_list;
    else line = line->next;

    if(line) return line->content.items;
    return 0;
} 

// plugs that are shipped with library
int tbe_rl_events(void) {
    int e = 0;
    static int erase_timer, goleft_timer, goright_timer;
    if (IsKeyDown(KEY_BACKSPACE)) erase_timer++; else erase_timer = 0;
    if (IsKeyDown(KEY_LEFT)) goleft_timer++; else goleft_timer = 0;
    if (IsKeyDown(KEY_RIGHT)) goright_timer++; else goright_timer = 0;

    if (    IsKeyPressed(KEY_BACKSPACE) ||
            erase_timer > 30 && 
            erase_timer % 3)
        e |= TBE_ACTION_ERASE;

    if (IsKeyPressed(KEY_LEFT) && IsKeyDown(KEY_LEFT_CONTROL))
        e |= TBE_ACTION_JUMP_LINE_START;
    if (IsKeyPressed(KEY_RIGHT) && IsKeyDown(KEY_LEFT_CONTROL))
        e |= TBE_ACTION_JUMP_LINE_END;
    
    if (IsKeyPressed(KEY_UP) && IsKeyDown(KEY_LEFT_CONTROL))
        e |= TBE_ACTION_JUMP_TEXT_START;
    if (IsKeyPressed(KEY_DOWN) && IsKeyDown(KEY_LEFT_CONTROL))
        e |= TBE_ACTION_JUMP_TEXT_END;

    if (IsKeyPressed(KEY_LEFT) || goleft_timer > 30)
        e |= TBE_ACTION_LEFT;
    if (IsKeyPressed(KEY_RIGHT) || goright_timer > 30)
        e |= TBE_ACTION_RIGHT;

    if (IsKeyPressed(KEY_UP))
        e |= TBE_ACTION_UP;
    if (IsKeyPressed(KEY_DOWN))
        e |= TBE_ACTION_DOWN;

    if (IsKeyPressed(KEY_ENTER) && IsKeyDown(KEY_LEFT_CONTROL))
        e |= TBE_ACTION_BREAKLINE;
    return e;
}



tbe_Context prompt = {0};

// yeah
void frame(void) {
    //const int ev[2] = {GetCharPressed(),0};
    const int cp = GetCharPressed();
    int ret_count = 0;
    const char* ch = CodepointToUTF8(cp, &ret_count);
    tbe_edit(&prompt, tbe_rl_events(), ch);
    //DrawText("Test\nMore.", 10, 10, 24*1, RAYWHITE);
    DrawText(prompt.as_string, 10, 10, 48*1, RAYWHITE);
   
    const char* l = tbe_cursor_left_text(&prompt);
    const char* r = tbe_cursor_right_text(&prompt);

    const char* line = 0;
    int counter = 3;
    while((line = tbe_get_line(&prompt))) {
        DrawText(line, 10, GetScreenHeight() - 48*counter - 10, 48*1, RAYWHITE);
        counter++;
    }
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
    int* c = tbe_cursor(&prompt);

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
