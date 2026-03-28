#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define LOGIN "/usr/sbin/login"

extern char **environ;

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <tty>\n", argv[0]);
    return EXIT_FAILURE;
  }
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  // tcsetpgrp() sends SIGTTOU to background callers, ignore it so we can
  // claim the terminal foreground without being stopped
  signal(SIGTTOU, SIG_IGN);

  // move into own process group (stay in host session so tcsetpgrp works)
  setpgid(0, 0);
  int ttyfd = open(argv[1], O_RDWR);
  if (ttyfd < 0) {
    fprintf(stderr, "(%s: open '%s': %s)\n", argv[0], argv[1], strerror(errno));
    abort();
  }
  ioctl(ttyfd, TIOCSCTTY, 0);
  tcsetpgrp(ttyfd, getpgrp());

  // loop to spawn login process
  for (;;) {
    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "(%s: fork: %s)\n", argv[0], strerror(errno));
      abort();
    }
    if (pid == 0) {
      // we are the child process, execve login
      dup2(ttyfd, STDIN_FILENO);
      dup2(ttyfd, STDOUT_FILENO);
      dup2(ttyfd, STDERR_FILENO);
      char *const loginargv[] = {LOGIN, NULL};
      if (execve(LOGIN, loginargv, environ) < 0)
        fprintf(stderr, "(%s: execve: %s)\n", argv[0], strerror(errno));
      abort();
    }

    // wait for child process to exit
    int status;
    if (wait(&status) < 0) {
      fprintf(stderr, "(%s: wait: %s)\n", argv[0], strerror(errno));
      abort();
    }
    // log if child exited with bad condition
    if (WIFSIGNALED(status)) {
      switch (WTERMSIG(status)) {
      case SIGSEGV:
        fprintf(stderr, "(%s: child segmentation fault)\n", argv[0]);
        abort();
      case SIGABRT:
        fprintf(stderr, "(%s: child aborted)\n", argv[0]);
        abort();
      default:
      }
    }
  }

  return EXIT_SUCCESS;
}
