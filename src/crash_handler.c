#include "crash_handler.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int crash_log_fd = -1;
static stack_t crash_altstack = {0};
static volatile sig_atomic_t crash_handling;

static void append_str(char *buf, size_t cap, size_t *off, const char *s)
{
    if (!buf || !off || !s || *off >= cap)
    {
        return;
    }
    while (*s && *off + 1 < cap)
    {
        buf[*off] = *s;
        (*off)++;
        s++;
    }
}

static void append_u64(char *buf, size_t cap, size_t *off, uint64_t v)
{
    char tmp[32];
    size_t n = 0;
    if (v == 0)
    {
        append_str(buf, cap, off, "0");
        return;
    }
    while (v > 0 && n < sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0)
    {
        char c[2] = {tmp[--n], '\0'};
        append_str(buf, cap, off, c);
    }
}

static void append_hex_u64(char *buf, size_t cap, size_t *off, uint64_t v)
{
    char tmp[32];
    size_t n = 0;
    static const char *hex = "0123456789abcdef";
    if (v == 0)
    {
        append_str(buf, cap, off, "0");
        return;
    }
    while (v > 0 && n < sizeof(tmp))
    {
        tmp[n++] = hex[v & 0xfu];
        v >>= 4u;
    }
    while (n > 0)
    {
        char c[2] = {tmp[--n], '\0'};
        append_str(buf, cap, off, c);
    }
}

static void crash_write_marker(int signo, siginfo_t *info)
{
    char msg[256];
    size_t off = 0;
    append_str(msg, sizeof(msg), &off, "stackcomp fatal signal ");
    append_u64(msg, sizeof(msg), &off, (uint64_t)signo);
    append_str(msg, sizeof(msg), &off, " pid=");
    append_u64(msg, sizeof(msg), &off, (uint64_t)getpid());
    if (info)
    {
        append_str(msg, sizeof(msg), &off, " addr=0x");
        append_hex_u64(msg, sizeof(msg), &off, (uint64_t)(uintptr_t)info->si_addr);
    }
    append_str(msg, sizeof(msg), &off, "\n");

    if (off > sizeof(msg))
    {
        off = sizeof(msg);
    }
    (void)write(STDERR_FILENO, msg, off);
    if (crash_log_fd >= 0)
    {
        (void)write(crash_log_fd, msg, off);
    }
}

static void crash_signal_handler(int signo, siginfo_t *info, void *ucontext)
{
    (void)ucontext;
    if (crash_handling)
    {
        _exit(128 + signo);
    }
    crash_handling = 1;

    crash_write_marker(signo, info);

    struct sigaction sa = {0};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(signo, &sa, NULL);
    (void)kill(getpid(), signo);
    _exit(128 + signo);
}

static bool install_one_signal(int signo)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = crash_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    return sigaction(signo, &sa, NULL) == 0;
}

void stackcomp_crash_handler_fini(void)
{
    if (crash_log_fd >= 0)
    {
        close(crash_log_fd);
        crash_log_fd = -1;
    }
    if (crash_altstack.ss_sp)
    {
        stack_t disabled = {0};
        disabled.ss_flags = SS_DISABLE;
        (void)sigaltstack(&disabled, NULL);
        free(crash_altstack.ss_sp);
        crash_altstack.ss_sp = NULL;
        crash_altstack.ss_size = 0;
    }
}

bool stackcomp_crash_handler_install(const char *log_path)
{
    if (log_path && log_path[0])
    {
        int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd < 0)
        {
            return false;
        }
        crash_log_fd = fd;
    }

    void *sp = malloc(SIGSTKSZ * 4);
    if (!sp)
    {
        stackcomp_crash_handler_fini();
        return false;
    }
    crash_altstack.ss_sp = sp;
    crash_altstack.ss_size = SIGSTKSZ * 4;
    crash_altstack.ss_flags = 0;
    if (sigaltstack(&crash_altstack, NULL) != 0)
    {
        stackcomp_crash_handler_fini();
        return false;
    }

    if (!install_one_signal(SIGSEGV) ||
        !install_one_signal(SIGABRT) ||
        !install_one_signal(SIGBUS) ||
        !install_one_signal(SIGILL) ||
        !install_one_signal(SIGFPE) ||
        !install_one_signal(SIGTRAP))
    {
        stackcomp_crash_handler_fini();
        return false;
    }

    (void)atexit(stackcomp_crash_handler_fini);
    return true;
}
