#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t childpid;

static void sigforward(int sig) {
  if (childpid > 0)
    kill(childpid, sig);
}

static void mkdev(const char *path, mode_t mode, unsigned maj, unsigned min) {
  mknod(path, S_IFCHR | mode, makedev(maj, min));
}

static int childmain(const char *rootfs, const char *ttypath) {
  char path[PATH_MAX];

  /* put the child in its own process group (but keep the host session) so
     that mocker's pgrp doesn't receive Ctrl+C once getty takes over as the
     terminal foreground; staying in the host session lets tcsetpgrp work */
  setpgid(0, 0);

  // mount rootfs to itself (pivot_root requires it to be a mount point)
  if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) < 0) {
    perror("mount --bind rootfs");
    return 1;
  }
  // the bind mount inherits MS_NOSUID from the workspace mount (VS Code
  // devcontainers mount workspaces nosuid); remount to clear it so setuid
  // binaries like sudo work inside the container
  if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REMOUNT, NULL) < 0)
    perror("mount --remount rootfs (setuid may not work)");

  // mount rootfs/dev
  snprintf(path, sizeof(path), "%s/dev", rootfs);
  mkdir(path, 0755);
  if (mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_STRICTATIME,
            "mode=755,size=64k") < 0) {
    perror("mount tmpfs /dev");
    return 1;
  }

  // create important kernel devices on rootfs/dev
  snprintf(path, sizeof(path), "%s/dev/null", rootfs);
  mkdev(path, 0666, 1, 3);
  snprintf(path, sizeof(path), "%s/dev/zero", rootfs);
  mkdev(path, 0666, 1, 5);
  snprintf(path, sizeof(path), "%s/dev/random", rootfs);
  mkdev(path, 0666, 1, 8);
  snprintf(path, sizeof(path), "%s/dev/urandom", rootfs);
  mkdev(path, 0666, 1, 9);
  snprintf(path, sizeof(path), "%s/dev/tty", rootfs);
  mkdev(path, 0666, 5, 0);

  // bind-mount host tty to /dev/console
  snprintf(path, sizeof(path), "%s/dev/console", rootfs);
  close(open(path, O_WRONLY | O_CREAT, 0620));
  if (ttypath[0]) {
    if (mount(ttypath, path, NULL, MS_BIND, NULL) < 0)
      perror("mount --bind console (continuing)");
  }

  // mount rootfs/proc
  snprintf(path, sizeof(path), "%s/proc", rootfs);
  mkdir(path, 0555);
  if (mount("proc", path, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) {
    perror("mount proc");
    return 1;
  }

  // mount rootfs/tmp
  snprintf(path, sizeof(path), "%s/tmp", rootfs);
  mkdir(path, 01777);
  mount("tmpfs", path, "tmpfs", 0, NULL);

  // open the rootfs mount as the root filesystem
  snprintf(path, sizeof(path), "%s/.old_root", rootfs);
  mkdir(path, 0700);
  if (syscall(SYS_pivot_root, rootfs, path) < 0) {
    perror("pivot_root");
    return 1;
  }
  if (chdir("/") < 0) {
    perror("chdir");
    return 1;
  }

  // unmount old root
  if (umount2("/.old_root", MNT_DETACH) < 0) {
    perror("umount /.old_root");
    return 1;
  }
  rmdir("/.old_root");

  // set the hostname
  sethostname("mocker", 6);

  // setup /dev/console as the stdin/stdout/stderr
  int fd = open("/dev/console", O_RDWR);
  if (fd >= 0) {
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
      close(fd);
  }

  // exec init
  char *argv[] = {"/usr/sbin/init", NULL};
  char *envp[] = {
      "HOME=/root",
      "PATH=/usr/bin:/usr/sbin",
      "TERM=linux",
      NULL,
  };
  execve("/usr/sbin/init", argv, envp);
  perror("execve /usr/sbin/init");
  return 1;
}

int main(int argc, char **argv) {
  const char *rootfs = "rootfs";
  if (argc > 1)
    rootfs = argv[1];

  // resolve rootfs absolute path (required by pivot_root)
  char rootfs_abs[PATH_MAX];
  if (!realpath(rootfs, rootfs_abs)) {
    fprintf(stderr, "mocker: %s: %s\n", rootfs, strerror(errno));
    return 1;
  }

  // save the tty path before entering new namespaces
  char ttypath[PATH_MAX] = "";
  char *tty = ttyname(STDIN_FILENO);
  if (tty)
    snprintf(ttypath, sizeof(ttypath), "%s", tty);

  // create new namespaces (PID, mount, UTS)
  if (unshare(CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS) < 0) {
    perror("mocker: unshare");
    fprintf(stderr,
            "hint: are you root? does the kernel support namespaces?\n");
    return 1;
  }

  // fork child to begin container
  childpid = fork();
  if (childpid < 0) {
    perror("mocker: fork");
    return 1;
  }
  if (childpid == 0)
    return childmain(rootfs_abs, ttypath);

  // parent:
  // - ignore SIGINT (Ctrl+C is handled by the container's foreground process)
  // - forward SIGTERM for graceful shutdown
  signal(SIGINT, SIG_IGN);
  struct sigaction sa = {0};
  sa.sa_handler = sigforward;
  sigaction(SIGTERM, &sa, NULL);

  // wait for the child to finish
  int status;
  waitpid(childpid, &status, 0);
  if (WIFEXITED(status))
    return WEXITSTATUS(status);

  return 1;
}
