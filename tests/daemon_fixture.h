#pragma once

#include "aether/control.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstdio>
#include <cstdlib>

#ifndef AETHERD_PATH
#error "AETHERD_PATH must be defined by CMake"
#endif

static void wait_for_socket() {
    for (int i = 0; i < 50; ++i) {
        struct stat st{};
        if (stat(aether::DAEMON_SOCKET_PATH, &st) == 0) return;
        usleep(100'000);
    }
    fprintf(stderr, "timeout waiting for daemon socket\n");
    std::abort();
}

struct DaemonFixture {
    pid_t pid;

    DaemonFixture() {
        unlink(aether::DAEMON_SOCKET_PATH);
        pid = fork();
        if (pid == 0) {
            int devnull = open("/dev/null", O_WRONLY);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
            execl(AETHERD_PATH, "aetherd", nullptr);
            _exit(1);
        }
        if (pid < 0) { perror("fork"); std::abort(); }
        wait_for_socket();
    }

    ~DaemonFixture() {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }
};
