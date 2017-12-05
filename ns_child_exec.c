/* ns_child_exec.c

   Copyright 2013, Michael Kerrisk
   Licensed under GNU General Public License v2 or later

   Create a child process that executes a shell command in new namespace(s).
*/
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <fcntl.h>


/* A simple error-handling function: print an error message based
   on the value in 'errno' and terminate the calling process */

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)

static int verbose = 0;
static int reaper = 0;

static void
usage(char *pname)
{
    fprintf(stderr, "Usage: %s [options] cmd [arg...]\n", pname);
    fprintf(stderr, "Options can be:\n");
    fprintf(stderr, "    -i   new IPC namespace\n");
    fprintf(stderr, "    -m   new mount namespace\n");
    fprintf(stderr, "    -n   new network namespace\n");
    fprintf(stderr, "    -p   new PID namespace\n");
    fprintf(stderr, "    -u   new UTS namespace\n");
    fprintf(stderr, "    -U   new user namespace\n");
    fprintf(stderr, "    -r   make cmd self a child subreaper\n");
    fprintf(stderr, "    -v   Display verbose messages\n");
    exit(EXIT_FAILURE);
}

static int              /* Start function for cloned child */
childFunc(void *arg)
{
    char **argv = arg;

    execvp(argv[0], &argv[0]);
    errExit("execvp");
}

#define STACK_SIZE (1024 * 1024)

static char child_stack[STACK_SIZE];    /* Space for child's stack */

/* Display wait status (from waitpid() or similar) given in 'status' */

/* SIGCHLD handler: reap child processes as they change state */

static void
child_handler(int sig)
{
    pid_t pid;
    int status;

    /* WUNTRACED and WCONTINUED allow waitpid() to catch stopped and
       continued children (in addition to terminated children) */

    while ((pid = waitpid(-1, &status,
                          WNOHANG | WUNTRACED | WCONTINUED)) != 0) {
        if (pid == -1) {
            if (errno == ECHILD)        /* No more children */
                break;
            else
                perror("waitpid");      /* Unexpected error */
        }

        if (verbose)
            printf("ns_child_exec: SIGCHLD handler: PID %ld terminated\n",
                    (long) pid);
    }
}

int
enterProcessPidns(int pid)
{
    int pidns = -1, ret = -1;
    char path[512];

    sprintf(path, "/proc/%d/ns/pid", pid);
    pidns = open(path, O_RDONLY| O_CLOEXEC);
    if (pidns < 0) {
        perror("fail to open pidns");
        return -1;
    }

    if (setns(pidns, CLONE_NEWPID) < 0) {
        perror("setns pidns");
        return -1;
    }

    ret = fork();
    if (ret < 0) {
        perror("fork after setns pidns");
        return -1;
    } else if (ret > 0) {
        /* Parent */
        exit(EXIT_SUCCESS);
    }
    /* Child */

    close(pidns);
    return 0;
}

int
main(int argc, char *argv[])
{
    int flags, opt;
    pid_t child_pid;
    struct sigaction sa;

    flags = 0;

    /* Parse command-line options. The initial '+' character in
       the final getopt() argument prevents GNU-style permutation
       of command-line options. That's useful, since sometimes
       the 'command' to be executed by this program itself
       has command-line options. We don't want getopt() to treat
       those as options to this program. */

    while ((opt = getopt(argc, argv, "+imnpuUvr")) != -1) {
        switch (opt) {
        case 'i': flags |= CLONE_NEWIPC;        break;
        case 'm': flags |= CLONE_NEWNS;         break;
        case 'n': flags |= CLONE_NEWNET;        break;
        case 'p': flags |= CLONE_NEWPID;        break;
        case 'u': flags |= CLONE_NEWUTS;        break;
        case 'U': flags |= CLONE_NEWUSER;       break;
        case 'r': reaper = 1;                  break;
        case 'v': verbose = 1;                  break;
        default:  usage(argv[0]);
        }
    }

    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = child_handler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
        errExit("sigaction");

    if (reaper) {
        if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0)
            errExit("prctl PR_SET_CHILD_SUBREAPER");
        if (verbose)
            printf("%s, top parent set PR_SET_CHILD_SUBREAPER\n", argv[0]);
    }

    child_pid = clone(childFunc,
                    child_stack + STACK_SIZE,
                    flags | SIGCHLD, &argv[optind]);
    if (child_pid == -1)
        errExit("clone");

    if (verbose)
        printf("%s: PID of child created by clone() is %ld\n",
                argv[0], (long) child_pid);

    /* Parent falls through to here */

    /* fork() another one and enter child_pid's pid namespace */
    if ((flags & CLONE_NEWPID) && reaper) {
	int pid;
        pid = fork();
        if (pid == -1)
            errExit("fork");
        if (pid == 0) {         /* Child */
            int ret;
            if (enterProcessPidns(child_pid) < 0)
                    exit(EXIT_FAILURE);

            printf("%s: Child (PID %d) running in pid(%d)'s pidns\n", argv[0], getpid(), pid);

            ret = fork();
            if (ret < 0) {
                perror("fork inside pidns");
                exit(EXIT_FAILURE);
            } else if (ret == 0) {  /* Parent */
                exit(EXIT_SUCCESS);
            }
            /* Child */
            sleep(1);
            printf("%s: Child  (PID=%ld) now an orphan (parent PID=%ld)\n",
                    argv[0], (long) getpid(), (long) getppid());
            sleep(1);
            printf("%s: Child  (PID=%ld) terminating\n", argv[0], (long) getpid());
            exit(EXIT_SUCCESS);
        }
        /* Parent */
    }

    if (waitpid(child_pid, NULL, 0) == -1)      /* Wait for child */
        errExit("waitpid");

    if (verbose)
        printf("%s: terminating\n", argv[0]);
    exit(EXIT_SUCCESS);
}

