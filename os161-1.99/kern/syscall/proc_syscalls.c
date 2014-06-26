#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include "opt-A2.h"
#include <synch.h>
#include <spl.h>
#include <mips/trapframe.h>

#if OPT_A2
//struct lock *pidlock;



int sys_fork(struct trapframe *tf, int32_t * retval) {
	struct proc *newproc;
	struct addrspace *newspace;

	int x;
	newproc = proc_create_runprogram(curproc->p_name);
	if(newproc == NULL) {
		return ENOMEM;
	}

	x = as_copy(curproc->p_addrspace, &newspace);
	if(x == ENOMEM) {
		return ENOMEM;
	}
	newproc->p_addrspace = newspace;
	if(newproc->p_addrspace == NULL) {
		return ENOMEM;
	}
	newproc->ppid = curproc->pid;
	newproc->runornot = 1;
	addproclist(curproc->pid);
	struct trapframe *ts = kmalloc(sizeof(struct trapframe));
	memcpy(ts, tf, sizeof(struct trapframe *));
	x = thread_fork("this", newproc, enter_forked_process, ts, 0);
	if(x != 0){
		panic("something died after child's thread_fork");
		return EINVAL;
	}
	(*retval) = newproc->pid;
	curproc->childcount++;
	return (0);
	
}

#endif


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
//  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
#if OPT_A2
  proc_lock_acquire();
  notrunning(curproc->pid);
  addexitcode(curproc->pid, _MKWAIT(exitcode));
  if(pid_exists(parentpid(curproc->pid))) {
	  proc_cv_broadcast();
  } else {
	  removepid(curproc->pid);
	  //can remove pid
  }
  
#endif
 as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
 #if OPT_A2  
  proc_lock_release();
#endif
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
  *retval = curproc->pid;
  return (0);
#else
  *retval = 1;
  return(0);
#endif
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */
#if OPT_A2
  proc_lock_acquire();
  if(pid_exists(pid) == 0) { proc_lock_release(); return EINVAL; }
  if(options != 0){ proc_lock_release(); return EINVAL; }
  if(status != 0) { 
	  proc_lock_release();
	  return EINVAL;
  }
  while(runstatus(pid) != 0) {
	  proc_cv_wait();
  }
  proc_lock_release();
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return 0;
#else
  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
#endif
}
