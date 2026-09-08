/**
 * @file exec_utils.c
 * @brief Command execution utilities (Go: system/exec.go)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "exec_utils.h"
#include "telemetry.h"

int run_command_argv(char *output, size_t size, char *const argv[]) {
    if (!output || size == 0 || !argv || !argv[0]) return -1;

    /* 创建管道 */
    int pipefd[2];
    if (pipe(pipefd) == -1) return -1;

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* 子进程 */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    /* 父进程 */
    close(pipefd[1]);

    /* 读取输出 */
    size_t total = 0;
    ssize_t n;
    while (total < size - 1 && (n = read(pipefd[0], output + total, size - 1 - total)) > 0) {
        total += n;
    }
    output[total] = '\0';
    close(pipefd[0]);

    /* 等待子进程 */
    int status;
    waitpid(pid, &status, 0);

    /* 去除尾部空白 */
    while (total > 0 && (output[total-1] == '\n' || output[total-1] == '\r' || output[total-1] == ' ')) {
        output[--total] = '\0';
    }

    int success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    telemetry_capture_command(argv[0], output, success);
    return success ? 0 : -1;
}

int run_command(char *output, size_t size, const char *cmd, ...) {
    va_list args;
    char *argv[32];
    int argc = 0;

    if (!cmd) return -1;
    argv[argc++] = (char *)cmd;
    va_start(args, cmd);
    char *arg;
    while ((arg = va_arg(args, char *)) != NULL && argc < 31) {
        argv[argc++] = arg;
    }
    va_end(args);
    argv[argc] = NULL;
    return run_command_argv(output, size, argv);
}

int run_command_timeout(int timeout_sec, char *output, size_t size, const char *cmd, ...) {
    /* 简化实现：直接调用 run_command */
    /* TODO: 实现真正的超时机制 */
    va_list args;
    va_start(args, cmd);
    int ret = run_command(output, size, cmd, args);
    va_end(args);
    return ret;
}

void device_reboot(void) {
    char buf[64];
    run_command(buf, sizeof(buf), "reboot", NULL);
}

void device_poweroff(void) {
    char buf[64];
    run_command(buf, sizeof(buf), "poweroff", NULL);
}

int clear_cache(void) {
    char buf[64];
    run_command(buf, sizeof(buf), "sync", NULL);
    return run_command(buf, sizeof(buf), "sh", "-c", "echo 3 > /proc/sys/vm/drop_caches", NULL);
}
