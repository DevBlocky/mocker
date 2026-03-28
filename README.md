# Mocker Linux Distro Experiment

This project creates a custom bare linux distro environment using linux namespaces.

The bare linux environment is created by `mocker.c`, which similar to Docker. It 
uses linux namespaces to create new PID/mount/UTS namespaces that containerize a
child instance from `rootfs/`. Mocker accomplishes this through linux syscalls
and filesystem manipulation to create an environment akin to a fresh linux
boot with no utilities or files. It uses `rootfs/usr/sbin/init` as the 
primary entrypoint into this environment.

Inside `rootfs` is the init, getty, login, and shell executables that are the 
backbone of the distro. Init handles creating the getty processes for each
tty in the system (only 1 in this case). Getty handles claiming control of
a tty and passing it to login. Login handles reading a username/password and
enabling a user environment for secure system login. Shell, obviously, works as 
the main interface for creating and running processes and performing system
functions.

## Custom Shell Utilities

(NOTE: Some utilities have options not listed here)

- `echo <message>` - Print message to STDOUT
- `env` - Print all environment variables
- `pwd` - Print the current working directory
- `sleep <t>` - Sleep for t seconds
- `cat [file]` - Open a file and read it to STDOUT
- `mv <src> <dst>` - Move a file or directory from src to dst
- `cp <src> <dst>` - Copy a file or directory from src to dst
- `rm <src>` - Remove a file or directory
- `ls [dir]` - List a directory's contents
- `mkdir <dir>` - Create a new directory
- `kill <pid>` - Send SIGTERM (or another signal) to a process
- `sudo <cmd...>` - Run a command as superuser (root)
- `su [login]` - Switch to another user
- `adduser <login>` - Create a new user
- `gpasswd <user> <group>` - Add or remove a user to a group
- `whoami` - Prints the current user
- `segfault` - Intentionally create a SIGSEGV condition
- `shutdown` - Send SIGTERM to init to end container

## Compile & Run

To compile `rootfs`:

```bash
make rootfs
```

To compile `mocker`:

```bash
make mocker
```

To start the container:

```bash
make run
```

### Using the Container

The default (and only) login for the container is root with no password:

```
login: root
password: 
/# echo hello world
hello world
/#
```

To exit the container, you can use the `shutdown` command:

```
login: root
password: 
/# shutdown
```

To create a new account and login as that account:

```
login: root
password:
/# adduser jacob
password: helloworld
confirm password: helloworld
/# gpasswd -a jacob sudo
/# exit
login: jacob
password: helloworld
/home/jacob# whoami
uid=1000 (jacob)
/home/jacob# shutdown
(shutdown: must be run as root)
/home/jacob# sudo shutdown
```
