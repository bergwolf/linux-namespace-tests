# Linux Namespace Playground

## Subreaper and pidns FUN

man(2) prctl says:
```
A  subreaper  fulfills  the  role  of  init(1) for its descendant processes.  When a process becomes orphaned (i.e., its immediate parent terminates) then that
process will be reparented to the nearest still living ancestor subreaper.  Subsequently, calls to getppid() in the orphaned process will now return the PID of
the  subreaper  process,  and  when  the  orphan  terminates, it is the subreaper process that will receive a SIGCHLD signal and will be able to wait(2) on the
process to discover its termination status.
```

However, does it work across different pid namespaces?

Test 1:
```
./ns_child_exec -p -v -r ./simple_init -v
./ns_child_exec, top parent set PR_SET_CHILD_SUBREAPER
./ns_child_exec: PID of child created by clone() is 66147
        init: my PID is 1
init$ ./orphan
        init: created child 2
Parent (PID=2) created child with PID 3
Parent (PID=2; PPID=1) terminating

Child  (PID=3) now an orphan (parent PID=1)
Child  (PID=3) terminating
```

Test 2:
```
./ns_child_exec -p -v -r ./simple_init -v -r
./ns_child_exec, top parent set PR_SET_CHILD_SUBREAPER
./ns_child_exec: PID of child created by clone() is 66142
        init: myself is set as subchild reaper
        init: my PID is 1
init$ ./orphan
        init: created child 2
Parent (PID=2) created child with PID 3
Parent (PID=2; PPID=1) terminating
        init: SIGCHLD handler: PID 2 terminated
init$
Child  (PID=3) now an orphan (parent PID=1)
Child  (PID=3) terminating
        init: SIGCHLD handler: PID 3 terminated
```

In both tests, `ns_child_exec` is running in the root ns and `simpgle_init` is running as pid 1 of a new pid namespace.

In test 1, `simple_init` does not handle `SIGCHLD` and the orphan processes are left zombie even though `ns_child_exec`
is registered as a child subreaper and handles `SIGCHLD`.

In test 2, `simple_init` handles `SIGCHLD` and thus the orphan processes created by `orphan` is properly reaped.

Therefore, we know that even though `prctl(PR_SET_CHILD_SUBREAPER)` can let a process fulfile the role of init(1) for
all of its descendant processes, the situation is not kept if any descendant process joins a different pid namespace.
