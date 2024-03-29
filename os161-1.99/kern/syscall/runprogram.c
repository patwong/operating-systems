/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include "opt-A2.h"
#include <copyinout.h>
/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */

#if OPT_A2
int runprogram(char *progname, char **args, unsigned long nargs) {
	//almost an exact copy of the second half of execv,
	//which copies all the arguments from args into the stack
	//difference: no argument copying
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	result = vfs_open(progname, O_RDONLY, (mode_t)0, &v);
	if (result) return result;

	//should be a brand new process
	KASSERT(curproc_getas() == NULL);

	//create new addrspace
    as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	//switch to new addrspace and activate it
	curproc_setas(as);
	as_activate();

	//load the exectuable file
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	//done with the file
	vfs_close(v);

	//define the userstack
	result = as_define_stack(curproc->p_addrspace, &stackptr);
	if(result) return result;

	//copy the individual strings in args into the real stack
	if(args != NULL) {
		vaddr_t argsptr[nargs+1];
		int y = nargs - 1;
		int argsstrlen = 0;
		int argsstrspace = 0;

		//the given stackptr starts at the top
		while(y >= 0) {
			//end of every string is filled with unimportant values
			//it is of size 4 - (arg length mod four)
			argsstrlen = strlen(args[y]) + 1;
			argsstrspace = 4 - (argsstrlen % 4);

			//shift stackptr to start of stack location where arg can be copied
			stackptr = stackptr - argsstrlen - argsstrspace;

			//and copy stack array into real stack
			result = copyoutstr(args[y], (userptr_t)stackptr, argsstrlen, NULL);
			if(result) return result;

			//point to the next item in args
			argsptr[y] = stackptr;
			y--;
		}

		//the top of the stack array will be pointing to nothing
		argsptr[nargs] = 0;
		y = nargs;

		//bottom part of stack is list of pointers
		while(y >= 0) {
		//so decrement by its size and copy the pointers onto stack
			stackptr = stackptr - sizeof(vaddr_t);
			result = copyout(&argsptr[y], (userptr_t)stackptr, sizeof(vaddr_t));
			if(result) return result;
			y--;
		}
	}

	//become the new process
	enter_new_process(nargs, (userptr_t)stackptr, stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

}
#else
int
runprogram(char *progname)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(curproc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
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
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
#endif
