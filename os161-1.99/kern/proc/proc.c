/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <synch.h>
#include <kern/fcntl.h>  
#include "opt-A2.h"
#include <limits.h>
#include <kern/errno.h>
/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;


/*
 * Mechanism for making the kernel menu thread sleep while processes are running
 */
#ifdef UW
/* count of the number of processes, excluding kproc */
static unsigned int proc_count;
/* provides mutual exclusion for proc_count */
/* it would be better to use a lock here, but we use a semaphore because locks are not implemented in the base kernel */ 
static struct semaphore *proc_count_mutex;
/* used to signal the kernel menu thread when there are no processes */
struct semaphore *no_proc_sem;   
#endif  // UW


#if OPT_A2
//my code
//proclist is a linked list which contains the current process':
//	pid, parent's pid, exitcode, run status, and pointer to the next
//	node
struct proclist *procstats;

//list of locks: each lock and cv associated with a pid
struct locklist *listoflocks;

//********both proclist and locklist structs defined in proc.h********//

//gets the exitcode of the process specified by pid
int getexitcode(pid_t pid) {
	struct proclist *node;
	node = procstats;
	while(node->mypid != pid) {
		node = node->next;
	}
	return node->exitcode;
}

//checks if the current process is running, returns 1 if running,
//0 if not running
int runstatus(pid_t pid) {
	struct proclist *curr;
	if(pid_exists(pid) == 0) {
		return EINVAL;
	}
	if(procstats == NULL) {
		return EINVAL;
	}
	curr = procstats;
	while(curr->mypid != pid) {
		curr = curr->next;
		if(curr == NULL) return EINVAL;
	}
	if(curr->mypid == pid) {
		return curr->runornot;
	}
	return EINVAL;
	//all this error checking is probably unnecessary
}

//change the current process' running status to 0
void notrunning(pid_t pid) {
	struct proclist *node;
	node = procstats;
	while(pid != node->mypid) {
		node = node->next;
	}
	if(pid == node->mypid) {
		node->runornot = 0;
	}
}


//adds a lock to locklist given pid
void addlock(pid_t ppid) {
	struct locklist *node;
	node = kmalloc(sizeof(struct locklist ));
	if(node == NULL) panic("\nnot enough mem!!!\n");
	node->ppid = ppid;
	node->lock = lock_create("locklist");
	node->cv = cv_create("locklist");
	node->next = NULL;
	if(listoflocks == NULL) {
		listoflocks = node;
	} else {
		struct locklist *curr;
		curr = listoflocks;
		while(curr->next != NULL) {
			curr = curr->next;
		}
		curr->next = node;
	}
}

//removes a lock from locklist given pid
void removelock(pid_t pid) {
	struct locklist *node;
	struct locklist *prev;
	node = listoflocks;
//	prev = node;
	if(node->ppid == pid) {
		listoflocks = listoflocks->next;
	} else {
		while(node->ppid != pid) {
			prev = node;
			node = node->next;
		}
		prev->next = node->next;
	}
	lock_destroy(node->lock);
	cv_destroy(node->cv);
	kfree(node);
}
//retrieves a lock associated with given pid
struct lock *lockretrieve(pid_t ppid) {
	if(pid_exists(ppid) == 0) panic("\ninvalid pid!\n");
	struct locklist *node;
	node = listoflocks;
	while(node->ppid != ppid) {
		node = node->next;
	}
	return node->lock;
}

//retrieves a cv associated with given pid
struct cv *cvretrieve(pid_t ppid) {
	struct locklist *node;
	node = listoflocks;
	while(node->ppid != ppid) {
		node = node->next;
	}
	return node->cv;
}

//checks if the given process' pid is curproc's child
int ismychild(pid_t pid) {
	struct proclist *node;
	node = procstats;
	while(node->mypid != pid) {
		node = node->next;
	}
	if(curproc->pid != node->ppid) {
		return 0; //not my child
	} 
	return 1; //my child
}
//creates a node for proclist 
struct proclist *new_pid_node(void) {
	struct proclist *node;
	node = kmalloc(sizeof(struct proclist ));
	node->ppid = 0;
	node->mypid = 0;
	node->exitcode = 0;
	node->runornot = 0;
	node->next = NULL;
	return node;
}

//creates a pid for the new process and adds it to procstats
pid_t pidcreator(void) {

	//initializes the first process that will use procstats
	if(procstats == NULL) {
		procstats = new_pid_node();
		if(procstats == NULL){
			return ENOMEM;
		}
		procstats->mypid = __PID_MIN;
		procstats->next = NULL;
		return procstats->mypid;
	}

	struct proclist *curr;
	struct proclist *after_curr;
	struct proclist *node;
	curr = procstats;
	after_curr = curr->next;
	node = NULL;

	//gets the last node of procstats
	//procstats sorted numerically low-to-high
	//unused pid exists if difference between two pid nodes > 1
	if(after_curr == NULL) {
		node = new_pid_node();
		node->mypid = curr->mypid + 1;
		node->next = NULL;
		curr->next = node;
		return node->mypid;
	} else {
		while(after_curr != NULL) {
	//		kprintf("\nafter_curr: %d, after_curr_exit: %d, a_c_run: %d, ac_parent: %d, curr: %d\n", 
	//				after_curr->mypid, after_curr->exitcode, after_curr->runornot, after_curr->ppid, curr->mypid);
			if(after_curr->mypid - curr->mypid > 1) {
				node = new_pid_node();
				node->mypid = curr->mypid + 1;
				curr->next = node;
				node->next = after_curr;
				return node->mypid;
			}
			curr = after_curr;
			after_curr = after_curr->next;
		}
	}
	if(curr->mypid >= __PID_MAX) {
		return EINVAL;
	}
	

	//append a new pid node to the end of procstats
	node = new_pid_node();
	node->mypid = curr->mypid + 1;
	if(node->mypid > __PID_MAX){
		//panic("\nout of pids!!!\n");
		return EINVAL;
	}
	node->next = NULL;
	curr->next = node;
	return node->mypid;
}

//finds the the given pid and removes the node and pid from procstats
int removepid(pid_t pid) {
	struct proclist *curr;
	struct proclist *prev;
	curr = procstats;
	prev = curr;


	if(procstats == NULL) return EINVAL;

	//if the first item in procstats is item that should be removed
	//returns 1 on success, else fails
	if(pid == curr->mypid) {
		procstats = curr->next;
		kfree(curr);
		return 1;
	} else if(curr->next != NULL) {
		curr = curr->next;
		while(curr != NULL) {
			if(pid == curr->mypid) {
				prev->next = curr->next;
				kfree(curr);
				return 1;
			}
			prev = curr;
			curr = curr->next;
		}
	} else {
		return EINVAL;
//		panic("that pid doesn't exist!!\n"); //change to return EINVAL
	}
	return EINVAL;
//	panic("something wrong with pid\n");
}
int validpid(pid_t pid) {
	if(pid < __PID_MIN || pid > __PID_MAX) {
		return 0;
	}
	return 1;
}

//checks if given pid is valid and if it exists
int pid_exists(pid_t pid) {
	if(validpid(pid) == 0) {
		return 0;
	}
	if(procstats == NULL) {
		return 0;
	}
	struct proclist *curr;
	curr = procstats;
	while(curr != NULL) {
		if(pid == curr->mypid) {
			return 1;			//pid exists
		}
		curr = curr->next;
	}
	return 0;					//pid doesn't exist
}

//adds the process' parent to its entry in procstats
//also sets the run status of the process to be 1 (running)
void addproclist(pid_t pid, pid_t ppid) {
	struct proclist *node;
	node = procstats;
	while(node->mypid != pid) {
		node = node->next;
	}
	node->ppid = ppid;
	node->exitcode = 0;	//exitcode will be 0 until process exits
	node->runornot = 1;
	
}

//associates the process to its exitcode
void addexitcode(pid_t pid, int exitcode) {
	struct proclist *node;
	node = procstats;
	while(node->mypid != pid) {
		node = node->next;
	}
	node->exitcode = exitcode;
}
#endif


/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

#ifdef UW
	proc->console = NULL;
#endif // UW
#if OPT_A2
	proc->ppid = 0;
	proc->pid = pidcreator();
#endif

	return proc;
}

/*
 * Destroy a proc structure.
 */
void
proc_destroy(struct proc *proc)
{
	/*
         * note: some parts of the process structure, such as the address space,
         *  are destroyed in sys_exit, before we get here
         *
         * note: depending on where this function is called from, curproc may not
         * be defined because the calling thread may have already detached itself
         * from the process.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

#ifndef UW  // in the UW version, space destruction occurs in sys_exit, not here
	if (proc->p_addrspace) {
		/*
		 * In case p is the currently running process (which
		 * it might be in some circumstances, or if this code
		 * gets moved into exit as suggested above), clear
		 * p_addrspace before calling as_destroy. Otherwise if
		 * as_destroy sleeps (which is quite possible) when we
		 * come back we'll be calling as_activate on a
		 * half-destroyed address space. This tends to be
		 * messily fatal.
		 */
		struct addrspace *as;

		as_deactivate();
		as = curproc_setas(NULL);
		as_destroy(as);
	}
#endif // UW

#ifdef UW
	if (proc->console) {
	  vfs_close(proc->console);
	}
#endif // UW

	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);

#ifdef UW
	/* decrement the process count */
        /* note: kproc is not included in the process count, but proc_destroy
	   is never called on kproc (see KASSERT above), so we're OK to decrement
	   the proc_count unconditionally here */
	P(proc_count_mutex); 
	KASSERT(proc_count > 0);
	proc_count--;
	/* signal the kernel menu thread if the process count has reached zero */
	if (proc_count == 0) {
	  V(no_proc_sem);
	}
	V(proc_count_mutex);
#endif // UW
	

}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
#if OPT_A2
  procstats = NULL;
  listoflocks = NULL;
#endif
  kproc = proc_create("[kernel]");
  if (kproc == NULL) {
    panic("\nproc_create for kproc failed\n");
  }
#if OPT_A2
  //remove its pid, set to 0;
  //kernel has special PID of 0; should not be seen by other procs 
  int checkremove = removepid(kproc->pid);
  if(checkremove != 1) {
	  panic("\nsomehow removepid failed\n");
  }
  kproc->pid = 0;
#endif

#ifdef UW
  proc_count = 0;
  proc_count_mutex = sem_create("proc_count_mutex",1);
  if (proc_count_mutex == NULL) {
    panic("could not create proc_count_mutex semaphore\n");
  }
  no_proc_sem = sem_create("no_proc_sem",0);
  if (no_proc_sem == NULL) {
    panic("could not create no_proc_sem semaphore\n");
  }
#endif // UW 
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *proc;
	char *console_path;

	proc = proc_create(name);
	if (proc == NULL) {
		return NULL;
	}

#ifdef UW
	/* open the console - this should always succeed */
	console_path = kstrdup("con:");
	if (console_path == NULL) {
	  panic("unable to copy console path name during process creation\n");
	}
	if (vfs_open(console_path,O_WRONLY,0,&(proc->console))) {
	  panic("unable to open the console during process creation\n");
	}
	kfree(console_path);
#endif // UW
	  
	/* VM fields */

	proc->p_addrspace = NULL;

	/* VFS fields */

#ifdef UW
	/* we do not need to acquire the p_lock here, the running thread should
           have the only reference to this process */
        /* also, acquiring the p_lock is problematic because VOP_INCREF may block */
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
#else // UW
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);
#endif // UW

#ifdef UW
	/* increment the count of processes */
        /* we are assuming that all procs, including those created by fork(),
           are created using a call to proc_create_runprogram  */
	P(proc_count_mutex); 
#if OPT_A2
	addlock(proc->pid);
#endif
	proc_count++;
	V(proc_count_mutex);
#endif // UW

	return proc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	t->t_proc = proc;
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			t->t_proc = NULL;
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

/*
 * Fetch the address space of the current process. Caution: it isn't
 * refcounted. If you implement multithreaded processes, make sure to
 * set up a refcount scheme or some other method to make this safe.
 */
struct addrspace *
curproc_getas(void)
{
	struct addrspace *as;
#ifdef UW
        /* Until user processes are created, threads used in testing 
         * (i.e., kernel threads) have no process or address space.
         */
	if (curproc == NULL) {
		return NULL;
	}
#endif

	spinlock_acquire(&curproc->p_lock);
	as = curproc->p_addrspace;
	spinlock_release(&curproc->p_lock);
	return as;
}

/*
 * Change the address space of the current process, and return the old
 * one.
 */
struct addrspace *
curproc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
