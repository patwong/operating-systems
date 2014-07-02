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
//	int plvl = splhigh();
	newproc = proc_create_runprogram("child");
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
	//sets the parent pid of the new proc to be the current proc
	newproc->ppid = curproc->pid;

	//initializing new node for the procstats list of information:
	//modify procstats to set the newproc's run status as "running": 1
	//newproc's parent pid is curproc's pid
	addproclist(newproc->pid, newproc->ppid);
	struct trapframe *ts;
	ts = kmalloc(sizeof(struct trapframe));
	if(ts == NULL) return ENOMEM;
	*ts = *tf;
//	memcpy(ts, tf, sizeof(struct trapframe));
	x = thread_fork("this", newproc, enter_forked_process, ts, 0);
	if(x != 0){
		kfree(ts);
		panic("\nsomething died after child's thread_fork\n");
		return EINVAL;
	}
	(*retval) = newproc->pid;
	threadarray_init(&newproc->p_threads);
	return (0);
	
}

#endif



void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
#if OPT_A2
  
	struct lock *lockitup;
	struct cv *cvwake; 

	
	//if parent exists, then lock and signal parent
	//otherwise no point in locking since there is no one to wake up
	if(pid_exists(curproc->ppid)) {
		//retrieves the lock and cv associated with the parent
		lockitup = lockretrieve(curproc->ppid);
		cvwake = cvretrieve(curproc->ppid);
		lock_acquire(lockitup);
		
		//change status to not running, add exitcode
		notrunning(curproc->pid);
		addexitcode(curproc->pid, _MKWAIT_EXIT(exitcode));
		
		//signal parent
		cv_signal(cvwake, lockitup);
		lock_release(lockitup);
	} else {
	//assumption: 
	//since parent exited, no one cares about curproc's exit status.
	//if curproc is exiting, then it doesn't care about its children
	//	so i can just remove its pid from the table;
	//	otherwise parent will remove it from the table in waitpid
		removepid(curproc->pid); 
	}

//when i should i remove the lock associated with curproc's pid?
//since curproc exiting, curproc is not going to call waitpid
//also no other proc will be waiting on curproc
//the lock associated with curproc can thus be removed
spinlock_acquire(&curproc->p_lock);
removelock(curproc->pid);
spinlock_release(&curproc->p_lock);

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
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}



int
sys_getpid(pid_t *retval)
{
#if OPT_A2
  //push the value of the current's process into retval
  *retval = curproc->pid;
  return (0);
#else
  *retval = 1;
  return(0);
#endif
}



int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

#if OPT_A2
  //gets the lock and cv associated with the current proc's pid
  struct lock *lockitup;
  lockitup = lockretrieve(curproc->pid);
  struct cv *cvsleep;
  cvsleep = cvretrieve(curproc->pid);

  lock_acquire(lockitup);
  //error cases
  //if the child doesn't exist, error
  if(pid_exists(pid) == 0) { lock_release(lockitup); return ESRCH; }

  //if options are not 0, error
  if(options != 0){ lock_release(lockitup); return EINVAL; }

  //if status != 0, error
  if(status != 0) { lock_release(lockitup);	return EFAULT; }
  //is it my child? 1 if my child, 0 if not
  if(ismychild(pid) != 1) { lock_release(lockitup); return EINVAL; }

  //if the child is still running, sleep
  while(runstatus(pid) != 0) {
	  cv_wait(cvsleep, lockitup);
  }
  exitstatus = getexitcode(pid);
  removepid(pid);		//child pid can be removed from list
  lock_release(lockitup);
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
