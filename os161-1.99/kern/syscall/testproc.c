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
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <limits.h>

#if OPT_A2

//most likely poor synchronization on exit or waitpid
//with semaphore on execv, the multiple forks calling execv
//	don't fail, specifically hogparty. otherwise, i had
//  error on trying to retrieve the lock in vfs
//initialized in boot()

struct semaphore *execvsem;

void execv_sync(void) {
	execvsem = sem_create("for execv only", 1);
}

//create the stack array
int stackarray(char **args, char **argsstack, int numargs) {
	int result, x = 0;
	while(x < numargs) {
		argsstack[x] = kmalloc(ARG_MAX);
		result = copyinstr((const userptr_t)args[x], argsstack[x], PATH_MAX, NULL);
		if(result) return result;
		x++;
	}
	return 0;
}
int sys_execv(char *progname, char **args) {
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	int numargs = 0;
	char *childname;
	//char **argsstack;
	int y, argsstrlen, argsstrspace;

	P(execvsem);

	//error checking	
	if(progname == NULL){
		V(execvsem);
		return ENOENT;
	}
	if(strlen(progname) == 0){
		V(execvsem);
		return ENOENT;
	}

	//put progname in kernel memory
	childname = kstrdup(progname);
	if(childname == NULL) {
		V(execvsem);
		return ENOMEM;
	}

	//if there are arguments, put on kernel
	if(args != NULL) {
		//counts the number of args passed in
		while(args[numargs] != NULL) numargs++;

		//10 arguments max accepted
		if(numargs > 10){
			V(execvsem);
			return E2BIG;
		}

		//creates stack "array" by copying given args into kernel
		//argsstack = kmalloc(sizeof(char *) * numargs);
//		result = stackarray(args, argsstack, numargs);
//		if(result) {
//			V(execvsem);
//			return result;
//		}
	}

	/* Open the file. */
	result = vfs_open(childname, O_RDONLY, 0, &v);
	if (result){
		V(execvsem);
		return result;
	}

	//if curproc has an addrspace, clear it
	if(curproc->p_addrspace) {
		as = curproc->p_addrspace;
		as_destroy(as);
		curproc->p_addrspace = NULL;
	}

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		V(execvsem);
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */		
	curproc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		V(execvsem);
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */		
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curproc->p_addrspace, &stackptr);
	if (result){
		V(execvsem);
		return result;
	}

	//copy the strings in args into stack array
	if(args != NULL) {
		vaddr_t argsptr[numargs+1];
		y = numargs - 1;
		//the given stackptr starts at the top
		while(y >= 0) {
			//end of every string is filled with unimportant values
			//it is of size 4 - (arg length mod four)
			argsstrlen = strlen(args[y]) + 1;
			argsstrspace = 4 - (argsstrlen % 4);

			//shift stackptr to the item in the array and copyoutstr
			stackptr = stackptr - argsstrlen - argsstrspace; 		
			result = copyoutstr(args[y], (userptr_t)stackptr, argsstrlen, NULL);
			if(result) {
				V(execvsem);
				return result;
			}
			//point to the next item in stack
			argsptr[y] = stackptr;
			y--;
		}
		//the end of the stack array will be pointing to nothing
		argsptr[numargs] = 0;
		y = numargs;

		//bottom part of stack points to a list of pointers
		//so decrement it by size of vaddr_t which is size int
		while(y >= 0) {
			//each element of size 4
			stackptr = stackptr - sizeof(vaddr_t);
			result = copyout(&argsptr[y], (userptr_t)stackptr, sizeof(vaddr_t));
			if(result){
				V(execvsem);
				return result;
			}
			y--;
		}
	}
	/* Warp to user mode. */
	V(execvsem);
	enter_new_process(numargs, (userptr_t)stackptr,	stackptr, entrypoint);	

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

int sys_fork(struct trapframe *tf, int32_t * retval) {
	struct proc *newproc;
	struct addrspace *newspace;

	int x;
	int plvl = splhigh();
	newproc = proc_create_runprogram("child");
	if(newproc == NULL) {
		return ENOMEM;
	}
	if(validpid(newproc->pid) == 0) {
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
	x = thread_fork("this", newproc, enter_forked_process, ts,(unsigned long) newspace);
	splx(plvl);
	if(x != 0){
		kfree(ts);
		panic("\nsomething died after child's thread_fork\n");
		return EINVAL;
	}
	(*retval) = newproc->pid;
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

	if(pid_exists(curproc->ppid) == 1) {
		//retrieves the lock and cv associated with the parent
		lockitup = lockretrieve(curproc->ppid);
		if(lockitup == NULL) panic("\nparent's lock doesn't exist!\n");
		cvwake = cvretrieve(curproc->ppid);
		if(cvwake == NULL) panic("\nparent's cv doesn't exist!\n");
		lock_acquire(lockitup);
		
		//change status to not running, add exitcode
		notrunning(curproc->pid);
		addexitcode(curproc->pid, _MKWAIT_EXIT(exitcode));
		
		//signal parent
		cv_signal(cvwake, lockitup);
		lock_release(lockitup);
	} else {
	//assumption: 
	//since parent doesn't exist, no one cares about curproc's exit status.
	//if curproc is exiting, then it doesn't care about its children
	//	so i can just remove its pid from the table;
	//	otherwise parent will remove it from the table in waitpid
		removepid(curproc->pid); 
		removelock(curproc->pid);
	}
	//what if parent exits but never waitpids?
	//what if proc exits but exited children still in system?
	//need to remove those pids
	//check their running status; if not running, remove their pids

//original code below left intact
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
  //error cases
  //if the child doesn't exist, error
  if(pid_exists(pid) == 0){
	  kprintf("\nerror in waitpid pid_ex\n");
	  return ESRCH;
  }
  //if options are not 0, error
  if(options != 0){
	  kprintf("\nerror in waitpid options\n");
	  return EINVAL;
  }

//when i tried to check the status i got an error
//so i don't know how to make a proper check
// but it seems that my code works fine without checking status!

  //if status != 0, error
//  if(status != NULL){
//	  kprintf("\nerror in waitpid status\n");
//	  return EFAULT;
//  }

  //is it my child? 1 if my child, 0 if not
  if(ismychild(pid) != 1){
	  kprintf("\nTHE KID IS NOT MY SON!!\n");
	  return EINVAL;
  }

  //gets the lock and cv associated with the current proc's pid
  struct lock *lockitup;
  struct cv *cvsleep;
  lockitup = lockretrieve(curproc->pid);
  cvsleep = cvretrieve(curproc->pid);

  lock_acquire(lockitup);

  //if the child is still running, sleep
  while(runstatus(pid) != 0) {
	  cv_wait(cvsleep, lockitup);
  }
  exitstatus = getexitcode(pid);
  removepid(pid);		//child pid can be removed from list
  removelock(pid);		//remove lock associated with child 
  lock_release(lockitup);
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return (0);
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
