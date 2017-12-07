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

Test 3:
```
./ns_child_exec -p -v -r ./simple_init -v -r
./ns_child_exec, top parent set PR_SET_CHILD_SUBREAPER
./ns_child_exec: PID of child created by clone() is 69431
ns_child_exec: SIGCHLD handler: PID 69432 terminated
./ns_child_exec: Child (PID 2) running in pid(0)'s pidns
ns_child_exec: SIGCHLD handler: PID 3 terminated
./ns_child_exec: Child  (PID=2) now an orphan (parent PID=0)
        init: myself is set as subchild reaper
        init: my PID is 1
init$ ./ns_child_exec: Child  (PID=2) terminating
ns_child_exec: SIGCHLD handler: PID 69433 terminated
```

In both test 1 and test 2, `ns_child_exec` is running in the root ns and `simple_init` is running as pid 1 of a new pid namespace.

In test 1, `simple_init` does not handle `SIGCHLD` and the orphan processes are left zombie even though `ns_child_exec`
is registered as a child subreaper and handles `SIGCHLD`.

In test 2, `simple_init` handles `SIGCHLD` and thus the orphan processes created by `orphan` is properly reaped.

In test 3, `ns_child_exec` forks more children and let them enter `simple_init`'s pid namespace. We can see that these processes forked
by `ns_child_exec` are still reaper by the child subpreaper process `ns_child_exec`.

This is complementary to the statements in man(7) pid_namespaces:
```
A child process that is orphaned within the
namespace will be reparented to this process rather than init(1) (unless one of the  ancestors  of  the  child  in  the  same  PID  namespace  employed  the  prctl(2)
PR_SET_CHILD_SUBREAPER command to mark itself as the reaper of orphaned descendant processes).
```

Adding to above:
`If a descendant process of a child subreaper process joins a new pid namespace, it will still be reparented to the child subreaper process if it is orphaned.`
