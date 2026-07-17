#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>

static void puts_out(const char* s) { write(1, s, strlen(s)); }

static int read_line(char* buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c;
        int r = read(0, &c, 1);
        if (r <= 0) { if (i == 0) return -1; break; }
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = 0;
    return i;
}

static void execute(char* cmd) {
    static char cmd_copy[256];
    static char* argv[32];
    static char path[64];
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
    if (argc == 0) return;

    if (strcmp(argv[0], "cd") == 0) {
        const char* target = (argc >= 2) ? argv[1] : "@/";
        if (chdir(target) != 0) puts_out("cd: no such directory\n");
        return;
    }

    if (strcmp(argv[0], "exit") == 0) _exit(0);

    const char* redir_file = nullptr;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], ">") == 0) {
            if (i + 1 < argc) redir_file = argv[i + 1];
            argv[i] = nullptr; argc = i; break;
        }
    }

    bool has_ext = false;
    for (int i = 0; argv[0][i]; i++) if (argv[0][i] == '.') has_ext = true;
    path[0] = '#'; path[1] = '0'; path[2] = '/';
    int pl = 3;
    for (int i = 0; argv[0][i]; i++) path[pl++] = argv[0][i];
    if (!has_ext) { path[pl++] = '.'; path[pl++] = 'o'; }
    path[pl] = '\0';
    argv[0] = path;

    pid_t pid = fork();
    if (pid == 0) {
        if (redir_file) {
            int rfd = open(redir_file, O_CREAT | O_WRONLY | O_TRUNC);
            if (rfd >= 0) dup2(rfd, 1);
        }
        execv(path, argv);
		write(1, "failed to execute\n", 18);
        _exit(1);
    }
    wait();
}

int main() {
    char line[256];
    puts_out("myshell 0.2\n");
    while (true) {
        puts_out("myos> ");
        int n = read_line(line, sizeof(line));
        if (n <= 0) continue;
        execute(line);
    }
    return 0;
}
