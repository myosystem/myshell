#include <myos>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <pipe>
#include <sched.h>
#include <stdint.h>
#include "font.h"

#define WIN_W  800
#define WIN_H  500
#define SCALE  2
#define FW     (8 * SCALE)
#define FH     (8 * SCALE)
#define COLS   (WIN_W / FW)
#define ROWS   (WIN_H / FH)

#define BG_COLOR     0x1E1E2Eu
#define FG_COLOR     0xCDD6F4u
#define PROMPT_COLOR 0x89B4FAu
#define CURSOR_COLOR 0xF5C2E7u

static char     screen_chars [ROWS][COLS];
static uint32_t screen_colors[ROWS][COLS];
static int cur_row = 0, cur_col = 0;
static int prompt_row = 0, prompt_col = 0;

static char input_buf[COLS];
static int  input_len = 0;
static bool shift_held = false;

static Window* win;

static void draw_char_at(int col, int row, char c, uint32_t fg) {
    if ((unsigned char)c >= 128) c = '?';
    uint8_t* glyph = font8x8[(unsigned char)c];
    uint32_t* fb   = (uint32_t*)win->gbuf;
    int px = col * FW, py = row * FH;
    for (int r = 0; r < 8; r++) {
        uint8_t bits = glyph[r];
        for (int b = 0; b < 8; b++) {
            uint32_t color = (bits & (1 << (7 - b))) ? fg : BG_COLOR;
            for (int sy = 0; sy < SCALE; sy++)
                for (int sx = 0; sx < SCALE; sx++)
                    fb[(py + r*SCALE + sy)*WIN_W + (px + b*SCALE + sx)] = color;
        }
    }
}

static void redraw_row(int row) {
    for (int c = 0; c < COLS; c++)
        draw_char_at(c, row, screen_chars[row][c], screen_colors[row][c]);
}

static void flush_row(int row) {
    RECT r = { 0, (int64_t)(row * FH), WIN_W, FH };
    win->draw_frame(r);
}

static void clear_screen() {
    uint32_t* fb = (uint32_t*)win->gbuf;
    for (int i = 0; i < WIN_W * WIN_H; i++) fb[i] = BG_COLOR;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            screen_chars[r][c]  = ' ';
            screen_colors[r][c] = FG_COLOR;
        }
    cur_row = cur_col = 0;
}

static void scroll_up() {
    for (int r = 0; r < ROWS - 1; r++)
        for (int c = 0; c < COLS; c++) {
            screen_chars[r][c]  = screen_chars[r+1][c];
            screen_colors[r][c] = screen_colors[r+1][c];
        }
    for (int c = 0; c < COLS; c++) {
        screen_chars[ROWS-1][c]  = ' ';
        screen_colors[ROWS-1][c] = FG_COLOR;
    }
    cur_row = ROWS - 1;
    cur_col = 0;
}

static void newline() {
    cur_col = 0;
    if (cur_row < ROWS - 1) {
        cur_row++;
        for (int c = 0; c < COLS; c++) {
            screen_chars[cur_row][c]  = ' ';
            screen_colors[cur_row][c] = FG_COLOR;
        }
    } else {
        scroll_up();
    }
}

static void putc_term(char ch, uint32_t color) {
    if (ch == '\n') { newline(); return; }
    if (cur_col >= COLS) newline();
    screen_chars[cur_row][cur_col]  = ch;
    screen_colors[cur_row][cur_col] = color;
    draw_char_at(cur_col, cur_row, ch, color);
    cur_col++;
}

static void puts_term(const char* s, uint32_t color) {
    while (*s) putc_term(*s++, color);
}

static void redraw_input() {
    for (int c = prompt_col; c < COLS; c++) {
        screen_chars[prompt_row][c]  = ' ';
        screen_colors[prompt_row][c] = FG_COLOR;
    }
    for (int i = 0; i < input_len && (prompt_col + i) < COLS - 1; i++) {
        screen_chars[prompt_row][prompt_col + i]  = input_buf[i];
        screen_colors[prompt_row][prompt_col + i] = FG_COLOR;
    }
    int cpos = prompt_col + input_len;
    if (cpos < COLS) {
        screen_chars[prompt_row][cpos]  = '_';
        screen_colors[prompt_row][cpos] = CURSOR_COLOR;
    }
    redraw_row(prompt_row);
    flush_row(prompt_row);
}

static void show_prompt() {
    prompt_row = cur_row;
    puts_term("$ ", PROMPT_COLOR);
    prompt_col = cur_col;
    input_len  = 0;
    redraw_input();
}

static void execute(const char* cmd) {
    if (strcmp(cmd, "clear") == 0) {
        clear_screen();
        win->draw_frame(win->rect);
        show_prompt();
        return;
    }

    static char cmd_copy[256];
    static char* argv[32];
    static char  path[64];
    int argc = 0;

    int len = (int)strlen(cmd);
    for (int i = 0; i <= len; i++) cmd_copy[i] = cmd[i];

    char* p = cmd_copy;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = nullptr;
    if (argc == 0) { show_prompt(); return; }

    path[0] = '@'; path[1] = '/';
    int pl = 2;
    for (int i = 0; argv[0][i]; i++) path[pl++] = argv[0][i];
    path[pl++] = '.'; path[pl++] = 'o'; path[pl] = '\0';
    argv[0] = path;

    Pipe* pipe = new Pipe();
    pid_t pid = fork();
    if (pid == 0) {
        pipe->redirect_stdout();
        execv(path, argv);
        _exit(1);
    }

    pipe->close_write();
    newline();
    char buf[256];
    int n;
    while ((n = pipe->read(buf, 255)) > 0) {
        buf[n] = '\0';
        puts_term(buf, FG_COLOR);
    }
    delete pipe;
    wait();

    if (cur_col != 0) newline();
    show_prompt();
    win->draw_frame(win->rect);
}

static char apply_shift(char c) {
    switch (c) {
        case '1': return '!'; case '2': return '@'; case '3': return '#';
        case '4': return '$'; case '5': return '%'; case '6': return '^';
        case '7': return '&'; case '8': return '*'; case '9': return '(';
        case '0': return ')'; case '-': return '_'; case '=': return '+';
        case '[': return '{'; case ']': return '}'; case '\\': return '|';
        case ';': return ':'; case '\'': return '"'; case '`': return '~';
        case ',': return '<'; case '.': return '>'; case '/': return '?';
        default: return c;
    }
}

static void handle_key(uint32_t key) {
    if (key == KEY_LSHIFT || key == KEY_RSHIFT) { shift_held = true;  return; }

    if (key == '\b') {
        if (input_len > 0) { input_len--; redraw_input(); }
        return;
    }

    if (key == '\n') {
        for (int c = prompt_col; c < COLS; c++) {
            screen_chars[prompt_row][c]  = ' ';
            screen_colors[prompt_row][c] = FG_COLOR;
        }
        for (int i = 0; i < input_len; i++) {
            screen_chars[prompt_row][prompt_col + i]  = input_buf[i];
            screen_colors[prompt_row][prompt_col + i] = FG_COLOR;
        }
        redraw_row(prompt_row);
        flush_row(prompt_row);

        input_buf[input_len] = '\0';
        static char cmd[COLS + 1];
        strcpy(cmd, input_buf);
        input_len = 0;

        newline();
        if (cmd[0]) execute(cmd);
        else show_prompt();
        return;
    }

    if (key >= 32 && key < 128) {
        char c;
        if (key >= 'A' && key <= 'Z')
            c = shift_held ? (char)key : (char)(key + 32);
        else
            c = shift_held ? apply_shift((char)key) : (char)key;
        if (input_len < COLS - 3 && (prompt_col + input_len) < COLS - 1) {
            input_buf[input_len++] = c;
            redraw_input();
        }
    }
}

int main() {
    win = new Window({ 50, 30, WIN_W, WIN_H });

    uint32_t* fb = (uint32_t*)win->gbuf;
    for (int i = 0; i < WIN_W * WIN_H; i++) fb[i] = BG_COLOR;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            screen_chars[r][c]  = ' ';
            screen_colors[r][c] = FG_COLOR;
        }

    puts_term("myshell 0.1", PROMPT_COLOR);
    newline();
    show_prompt();
    win->draw_frame(win->rect);

    while (true) {
        wait_for_msg();
        msg_t msg;
        while (receive_msg(&msg) == 0) {
            if (msg.type == MSG_KEY_PRESS) {
                handle_key((uint32_t)msg.payload.params.arg[0]);
            } else if (msg.type == MSG_KEY_RELEASE) {
                uint32_t k = (uint32_t)msg.payload.params.arg[0];
                if (k == KEY_LSHIFT || k == KEY_RSHIFT) shift_held = false;
            }
        }
    }
}
