#define _GNU_SOURCE

#define HOSTNAME "container"
#define ROOTFS_DIR "ubuntu-rootfs"
#define PATH_BUFFER_SIZE 512
#define STACK_SIZE (1024 * 1024)

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>

static int g_argc;
static char **g_argv;
static char child_stack[STACK_SIZE];

struct child_args
{
    char **argv;
    int argc;
};

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (errno)
    {
        fprintf(stderr, ": %s\n", strerror(errno));
    }
    else
    {
        fputc('\n', stderr);
    }
    exit(1);
}

static void must(int rc, const char *what)
{
    if (rc < 0)
        die("%s failed", what);
}

static int ensure_dir(const char *path, mode_t mode)
{
    if (mkdir(path, mode) == 0)
        return 0;
    if (errno == EEXIST)
        return 0;
    return -1;
}

static void write_to_file(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd >= 0)
    {
        (void)write(fd, content, strlen(content));
        close(fd);
    }
}

static void handle_child_exit(int status)
{
    if (WIFEXITED(status))
    {
        printf("Child exited with status %d\n", WEXITSTATUS(status));
        exit(WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        printf("Child killed by signal %d\n", WTERMSIG(status));
        exit(128 + WTERMSIG(status));
    }
    else
    {
        exit(1);
    }
}

static void cg_join_and_limit(void)
{
    const char *cgroups = "/sys/fs/cgroup";
    char path[PATH_BUFFER_SIZE];

    snprintf(path, sizeof(path), "%s/pids", cgroups);
    if (access(path, F_OK) != 0)
    {
        return;
    }

    char cgroup_path[PATH_BUFFER_SIZE];
    snprintf(cgroup_path, sizeof(cgroup_path), "%s/pids/container", cgroups);
    if (ensure_dir(cgroup_path, 0755) < 0)
        return;

    char f[PATH_BUFFER_SIZE];

    snprintf(f, sizeof(f), "%s/pids.max", cgroup_path);
    write_to_file(f, "20");

    snprintf(f, sizeof(f), "%s/notify_on_release", cgroup_path);
    write_to_file(f, "1");

    snprintf(f, sizeof(f), "%s/cgroup.procs", cgroup_path);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", getpid());
    write_to_file(f, buf);
}

int child_main(void *arg)
{
    struct child_args *args = (struct child_args *)arg;

    char **new_argv = malloc((args->argc + 1) * sizeof(char *));
    if (!new_argv)
        die("malloc");

    new_argv[0] = "/proc/self/exe";
    new_argv[1] = "child";

    for (int i = 2; i < args->argc; i++)
    {
        new_argv[i] = args->argv[i];
    }
    new_argv[args->argc] = NULL;

    execv("/proc/self/exe", new_argv);
    perror("execv failed");

    free(new_argv);

    return 1;
}

void child()
{
    fprintf(stdout, "Running ");
    for (int i = 2; i < g_argc; i++)
    {
        fprintf(stdout, "%s%s", g_argv[i], (i + 1 < g_argc ? " " : ""));
    }
    fprintf(stdout, "\n");

    must(sethostname(HOSTNAME, strlen(HOSTNAME)), "sethostname");

    must(unshare(CLONE_NEWNS), "unshare(CLONE_NEWNS)");
    must(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL), "mount MS_PRIVATE");

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
        die("getcwd");

    char rootfs_path[PATH_MAX];
    if (snprintf(rootfs_path, sizeof(rootfs_path), "%s/%s", cwd, ROOTFS_DIR) >= (int)sizeof(rootfs_path))
        die("rootfs path too long");

    must(chroot(rootfs_path), "chroot");
    must(chdir("/"), "chdir /");

    (void)ensure_dir("proc", 0555);
    (void)ensure_dir("mytemp", 0755);

    must(mount("proc", "proc", "proc", 0, NULL), "mount proc");
    must(mount("thing", "mytemp", "tmpfs", 0, NULL), "mount tmpfs");

    cg_join_and_limit();

    pid_t pid = fork();
    if (pid < 0)
        die("fork");

    if (pid == 0)
    {
        if (g_argc < 3)
            die("no command provided");
        char **cmd_argv = &g_argv[2];
        execvp(cmd_argv[0], cmd_argv);
        perror("execvp");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        perror("waitpid");
    }

    must(umount2("proc", 0), "umount2 proc");
    must(umount2("mytemp", 0), "umount2 mytemp");

    handle_child_exit(status);
}

void run(int argc, char *argv[])
{
    printf("Running %s %s %s...\n", "(namespaces)", "(reexec)", "(wait)");
    struct child_args args = {
        .argv = argv,
        .argc = argc};

    int clone_flags = CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD;

    pid_t pid = clone(child_main, child_stack + STACK_SIZE, clone_flags, &args);

    if (pid == -1)
    {
        perror("clone failed");
        exit(1);
    }

    int status;
    if (waitpid(pid, &status, 0) == -1)
    {
        perror("waitpid failed");
        exit(1);
    }

    handle_child_exit(status);
}

int main(int argc, char *argv[])
{
    g_argc = argc;
    g_argv = argv;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <name>\n", argv[0]);
        return 1;
    }

    char *command = argv[1];

    if (strcmp(command, "run") == 0)
    {
        run(argc, argv);
    }
    else if (strcmp(command, "child") == 0)
    {
        child();
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n", command);
        exit(1);
    }
}