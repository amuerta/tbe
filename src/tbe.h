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

// TODO:
void        tbe_clear(tbe_Context* ctx);
//


void        tbe_edit(tbe_Context* ctx, int action, const char* input_text);
void        tbe_free(tbe_Context* ctx);

const int*  tbe_cursor(tbe_Context* ctx);
const char* tbe_cursor_left_text(tbe_Context* ctx);
const char* tbe_cursor_right_text(tbe_Context* ctx);
bool        tbe_get_line(tbe_Context* ctx, const char** line);
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
    TBE_ACTION_INSERT,
    TBE_ACTION_BREAKLINE,
    TBE_ACTION_ERASE,
    TBE_ACTION_UP,
    TBE_ACTION_DOWN,
    TBE_ACTION_LEFT,
    TBE_ACTION_RIGHT,
    TBE_ACTION_JUMP_LINE_END,
    TBE_ACTION_JUMP_LINE_START,
    TBE_ACTION_JUMP_TEXT_END,
    TBE_ACTION_JUMP_TEXT_START,
    TBE_ACTION_JUMP_WORD_BACK,
    TBE_ACTION_JUMP_WORD_FORWARD,
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
    if (!b->items) {
        assert(!b->items && "Zero initlized when capacity is 0");
        if (b->capacity == 0) b->capacity = 32; // default
        b->items = calloc(b->capacity, 1);
    }
    if (b->count + text_size > b->capacity) {
        size_t old_capacity = b->capacity;
        b->capacity = (old_capacity + text_size) * growth_factor;
        b->items = tbe_recalloc(b->items, 
                old_capacity, 
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
        dest->items = calloc(dest->capacity + 1, 1);
    }

    if (dest->count + src.count >= dest->capacity) {
        size_t prev_cap = dest->capacity;
        dest->capacity = (dest->count + src.count) * 2;
        dest->items = tbe_recalloc(dest->items, 
                prev_cap, 
                dest->capacity);
    }

    memcpy(dest->items + dest->count, src.items, src.count);
    dest->count+=src.count;
}

void tbe_line_insert_after(tbe_Line *l, tbe_Line* newline) {
    assert(l);
    tbe_Line *before = l->prev;
    tbe_Line *after = l->next;

    l->next = newline;
    newline->prev = l;

    if(after) {
        newline->next = after;
        after->prev = newline;
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

#define TBE_RESET_ACTION 0

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


// TODO: extract some of the functionality of this function into
// static inline functions?
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

    // one action per instance of time.
    switch (action) {
        case TBE_ACTION_JUMP_LINE_START: 
            {
                ctx->cursor_x = 0;
            } break;

        case TBE_ACTION_JUMP_LINE_END: 
            {
                size_t line_len    = ctx->line_current->content.count;
                ctx->cursor_x      = tbe_clamp(line_len, 0, line_len);
            } break;


        case TBE_ACTION_JUMP_TEXT_START: 
            {
                tbe_Line* iter = ctx->line_current;
                while(iter->prev) {
                    iter = iter->prev;
                    ctx->cursor_y--;
                }
                ctx->line_current = iter;
                ctx->cursor_x = 0;

                action = 0;
                goto rebuild_line;
            } break;

        case TBE_ACTION_JUMP_TEXT_END: 
            {
                tbe_Line* iter = ctx->line_current;
                while(iter->next) {
                    iter = iter->next;
                    ctx->cursor_y++;
                }
                ctx->line_current = iter;

                size_t line_len    = ctx->line_current->content.count;
                ctx->cursor_x      = tbe_clamp(line_len, 0, line_len);

                action = 0;
                goto rebuild_line;
            } break;


        case TBE_ACTION_UP: 
            {
                if (current_line->prev) {
                    ctx->line_current = ctx->line_current->prev;
                    size_t line_len    = ctx->line_current->content.count;
                    ctx->cursor_y--;
                    ctx->cursor_x = tbe_clamp(ctx->cursor_x, 0, line_len);
                    action = 0;
                    goto rebuild_line;
                }
            } break;

        case TBE_ACTION_DOWN: 
            {
                if (current_line->next) {
                    ctx->line_current = ctx->line_current->next;
                    size_t line_len    = ctx->line_current->content.count;
                    ctx->cursor_y++;
                    ctx->cursor_x = tbe_clamp(ctx->cursor_x, 0, line_len);
                    action = 0;
                    goto rebuild_line;
                }
            } break;
        
        case TBE_ACTION_BREAKLINE: 
            {
                if(!ctx->line_current->next && !buf->count) {
                    ctx->line_current->next = tbe_list_make_node();
                    tbe_Line* prev = ctx->line_current;
                    ctx->line_current = ctx->line_current->next;
                    ctx->line_current->prev = prev;
                    ctx->cursor_y++;
                    ctx->cursor_x = 0;
                    action = 0;
                    goto rebuild_line;
                }
                else if (ctx->line_current->next && at_line_end) {
                    tbe_line_insert_after(ctx->line_current, tbe_list_make_node());
                    ctx->line_current = ctx->line_current->next;
                    ctx->cursor_y++;
                    ctx->cursor_x = 0;//tbe_clamp(ctx->cursor_x, 0, line_len);
                    action = 0;
                    goto rebuild_line;
                } 
                else {
                    tbe_Line* after = tbe_list_make_node();
                    tbe_buffer_append_sized(&(after->content), 
                            ctx->cursor_right.data, 
                            ctx->cursor_right.length);
                    ctx->line_current->content.count = ctx->cursor_left.count;
                    tbe_line_insert_after(ctx->line_current, after);
                    ctx->line_current = after;
                    ctx->cursor_y++;
                    ctx->cursor_x = 0;//tbe_clamp(ctx->cursor_x, 0, line_len);
                    action = 0;
                    goto rebuild_line;
                }
            } break;

        case TBE_ACTION_ERASE: 
            {
                bool have_chars    = buf->count != 0;

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
                        
                        // !!
                        tbe_buffer_free(&(remove->content));
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

                    action = 0;
                    goto rebuild_line;
                }

                else if (!have_chars) {
                    if(current_line->prev) {
                        tbe_Line* move_to = ctx->line_current->prev;
                        tbe_Line* remove = tbe_line_remove_after(ctx->line_current->prev);
                        ctx->line_current = move_to;
                        ctx->cursor_y--;
                        int line_len = move_to->content.count;
                        ctx->cursor_x = tbe_clamp(line_len, 0, LINE_MAX_WIDTH);
                        
                        // !!
                        tbe_buffer_free(&(remove->content));
                        free(remove);
                        action = 0;
                        goto rebuild_line;
                    } 

                    else if (!current_line->prev && current_line->next) {
                        tbe_Line* old       = current_line;
                        ctx->line_current   = current_line->next;
                        ctx->line_current->prev = 0;
                        ctx->line_list = ctx->line_current;
                        // free content and node
                        // !!
                        tbe_buffer_free(&(old->content));
                        free(old);
                    }
                }

            } break;
        
        case TBE_ACTION_LEFT: 
            {
                ctx->cursor_x = tbe_clamp(ctx->cursor_x-1, 0, LINE_MAX_WIDTH);
            } break;
        
        case TBE_ACTION_RIGHT: 
            {
                ctx->cursor_x = tbe_clamp(ctx->cursor_x+1, 0, buf->count);
            } break;
        
        case TBE_ACTION_JUMP_WORD_BACK: 
        case TBE_ACTION_JUMP_WORD_FORWARD: 
               {
                   assert(0 && "TODO: implement word jumping");
               } break;

        default:
        case TBE_ACTION_INSERT: 
               {

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
               } break;

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

const int* tbe_cursor(tbe_Context* ctx) {
    enum { x, y };
    static int position[2];
    position[x] = ctx->cursor_x;
    position[y] = ctx->cursor_y;
    return position;
}

// TODO: should i rebuild C-string on each call of function or not?
// if not make this function actually do the building process.
const char* tbe_get_string(tbe_Context* ctx) {
    return ctx->as_string;
}

const char* tbe_cursor_left_text(tbe_Context* ctx) {
    return ctx->cursor_left.items;
}

const char* tbe_cursor_right_text(tbe_Context* ctx) {
    return ctx->cursor_right.data;
}

bool tbe_get_line(tbe_Context* ctx, const char** line_text) {
    assert(line_text && "expected line_text pointer to be not NULL");
    static tbe_Line* line;
    if (!line) line = ctx->line_list;
    else line = line->next;

    if(line) {
        *line_text = line->content.items;
        return true;
    }
    *line_text = 0;
    return false;
}
