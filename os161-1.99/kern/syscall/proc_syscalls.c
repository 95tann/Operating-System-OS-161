#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <vfs.h>
#include <array.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <test.h>

	/* this implementation of sys__exit does not do anything with the exit code */
	/* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

	DEBUG(DB_SYSCALL, "sys_exit() called.\n");
	DEBUG(DB_SYSCALL, "*************************************************\n");
	struct addrspace *as;
	struct proc *p = curproc;
	DEBUG(DB_SYSCALL, "sys_exit(): curproc->PID: %d\n",(int)curproc->pid);

	lock_acquire(process_info_arr_lk);
	unsigned int i = 0;
	while(array_num(process_info_arr) > i){
		struct process_info* pi = array_get(process_info_arr,i);

	  	if(p->pid == pi->pid){
	  		pi->has_exited = true;
	  		pi->exit_code = _MKWAIT_EXIT(exitcode);
	  		pi->dead = true;
	  	}
	  	i = i + 1;
	}
	lock_release(process_info_arr_lk);

	DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

	KASSERT(curproc->p_addrspace != NULL);
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
	broadcast_and_destroy(p);

	thread_exit();
	/* thread_exit() does not return, so we should never get here */
	panic("return from thread_exit in sys_exit\n");

	DEBUG(DB_SYSCALL, "sys_exit() completed.\n");
	DEBUG(DB_SYSCALL, "*************************************************\n");
}


/* stub handler for getpid() system call                */
int sys_getpid(pid_t *retval) {
	KASSERT(curproc != NULL);
	*retval = curproc->pid;
	return(0);
}

/* stub handler for waitpid() system call                */

int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval){

	DEBUG(DB_SYSCALL, "sys_waitpid() called.\n");

	int exitstatus;
	int result;

	/* this is just a stub implementation that always reports an
		 exit status of 0, regardless of the actual exit status of
		 the specified process.
		 In fact, this will return 0 even if the specified process
		 is still running, and even if it never existed in the first place.

		 Fix this!
	*/
	struct process_info* child_info = fetch_process_info(pid);
	struct proc* child = fetch_child(curproc->pid,pid);

	if (child_info == NULL || child == NULL){
		return ESRCH;
	}
    	if ((curproc == child_info->proc_pointer) || (curproc->pid != child_info->parent_pid)){
     		return ECHILD;
    	}
    	if (options != 0){
  	    	return(EINVAL);
    	}

    	lock_acquire(child_info->p_lk);
    	while(child_info->dead == false){
    		cv_wait(child->p_cv, child_info->p_lk);
    	}
    	exitstatus = child_info->exit_code;
    	lock_release(child_info->p_lk);

	result = copyout((void *)&exitstatus,status,sizeof(int));
	if (result) {
		return(result);
	}

	*retval = pid;
	DEBUG(DB_SYSCALL, "sys_waitpid(): about to terminate.\n");
	return(0);
}

// fork function implementation
int sys_fork(struct trapframe* tf, pid_t* retval){

	DEBUG(DB_SYSCALL, "sys_fork() called.\n");
	DEBUG(DB_SYSCALL, "*************************************************\n");

	struct proc* curr_p = curproc;
	struct proc* child = proc_create_runprogram(curr_p->p_name);
	DEBUG(DB_SYSCALL, "sys_fork(): curproc->pid %d\n",(int)curr_p->pid);

	if (child == NULL){
		DEBUG(DB_SYSCALL, "forking error, new process was not created.\n");
		return ENPROC;
	}
	if (child != NULL){
		DEBUG(DB_SYSCALL, "system fork() successful, new process was created.\n");
	}

	struct addrspace* curr_p_addrspace = curproc_getas();
	struct addrspace* child_addrspace;
	as_copy(curr_p_addrspace, &(child_addrspace));

	if (child_addrspace != NULL){
		child->p_addrspace = child_addrspace;
	}

	if(child->p_addrspace == NULL){
		kfree(child->p_addrspace);
		proc_destroy(child);
		DEBUG(DB_SYSCALL, "forking error, failed to create child process' address space.\n");
		return ENOMEM;
	}
	if(child->p_addrspace != NULL){
		DEBUG(DB_SYSCALL, "system fork(): created child process address space.\n");
		establish_parent_child(child,curr_p);
	}

	struct trapframe* child_trapf = kmalloc(sizeof(struct trapframe));
	if (child_trapf != NULL){
		memcpy(child_trapf, tf, sizeof(struct trapframe));
		DEBUG(DB_SYSCALL, "system fork(): child process trapframe created.\n");
	}
	if (child_trapf == NULL){
		DEBUG(DB_SYSCALL, "forking error, failure to create child process trapframe.\n");
		proc_destroy(child);
		return ENOMEM;
	}

	DEBUG(DB_SYSCALL, "sys_fork(): before thread_fork called.\n");
	int thread_fork_retval = thread_fork(curthread->t_name, child, enter_forked_process, child_trapf, 0);
	if(thread_fork_retval){
		DEBUG(DB_SYSCALL, "forking error, current process thread could not be forked.\n");
		return thread_fork_retval;
	}

	*retval = child->pid;
    int arr_size = array_num(process_info_arr);

    DEBUG(DB_SYSCALL, "system fork() procedure completed without errors.\n");
	DEBUG(DB_SYSCALL, "sys_fork(): size of process_info_arr at end of sys_fork = %d.\n", arr_size);
	DEBUG(DB_SYSCALL, "*************************************************\n");

	return 0;
}

// This is very similar to runprogram.c
// Hint: start by copying code from runprogram.c & then modifying it
int sys_execv(char* progname, char** args){
	
	DEBUG(DB_SYSCALL, "sys_execv() called.\n");
	DEBUG(DB_SYSCALL, "*************************************************\n");

	if (progname == NULL){
		return EFAULT;
	}

	const int VADDR = sizeof(vaddr_t);
	int result, offset;
	unsigned long argc, total_args;
	struct addrspace *as, *old_addrspace;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;

	// count num. of args
	argc = 0;
	while(args[argc] != NULL){
		argc = argc + 1;
	}
	total_args = argc + 1; // +1 to account for null terminator

    DEBUG(DB_SYSCALL, "sys_execv(): about to copy over args.\n");

    // copy progname into kernel progname
    char* kprogname = kmalloc(sizeof(char) * (strlen(progname)+1));
    if (kprogname == NULL){
    	return ENOMEM;
    }
    int copied_progname = copyinstr((userptr_t)progname,kprogname,(strlen(progname)+1),NULL);
    if (copied_progname) {
    	return copied_progname;
    }
    
    // create args array
	char** array_args = kmalloc(sizeof(char*) * (total_args));
	if (array_args == NULL){
		return ENOMEM;
	}
	DEBUG(DB_SYSCALL, "sys_execv(): finished allocating memory for array_args.\n");
	
    
    // copy args
    unsigned int i = 0;
	while(i <= total_args -2){
		int len = 1 + strlen(args[i]);
		array_args[i] = kmalloc(sizeof(char) * len);
		userptr_t src = (userptr_t) args[i];
		char* dest = array_args[i];
		int copied_result = copyinstr(src,dest,len,NULL);
		if (copied_result){
			return copied_result;
		}
		i = i+1;
	}
	DEBUG(DB_SYSCALL, "sys_execv(): finished copying args to array_args.\n");
	array_args[argc] = NULL;


    DEBUG(DB_SYSCALL, "sys_execv(): about to call vfs_open.\n");
	/* Open the file. */
	result = vfs_open(kprogname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

    DEBUG(DB_SYSCALL, "sys_execv(): about to create new addrspace.\n");
	/* Create a new address space. */
	old_addrspace = curproc_getas();  
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	curproc_setas(as);
	as_activate();
	
	DEBUG(DB_SYSCALL, "sys_execv(): about to destroy old addrspace.\n");
	as_destroy(old_addrspace);

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

	vaddr_t argsPtr_array[total_args];
    // 8-byte aligned
    offset = stackptr % 8;
	stackptr = stackptr - offset;
    
	i = 0;     
	while(i <= total_args -2){  
		offset = 1 + strlen(array_args[i]);
		stackptr = stackptr - offset;
		char* src = array_args[i];
		userptr_t dest = (userptr_t)stackptr;
		result = copyoutstr(src, dest, offset, NULL);
        if (result){
        	return result;
        }
		argsPtr_array[i] = stackptr;
		i = i+1;  
	}
    argsPtr_array[argc] = 0; // Set last ptr value to be 0
    
    DEBUG(DB_SYSCALL, "sys_execv(): finished copyoutstr to userspace.\n");
   
    // string stack 4-byte aligned
    offset = stackptr % 4;
    stackptr = stackptr - offset;

    i = 0;
    while(i < total_args){
        offset = ROUNDUP(VADDR,4);
        stackptr = stackptr - offset;
		userptr_t dest = (userptr_t)stackptr;
        result = copyout(&argsPtr_array[i],dest,VADDR);
        if (result){
        	return result;
        }
    	i = i+1;    
    }

	/* Warp to user mode. */
	userptr_t argv = (userptr_t)stackptr;
	DEBUG(DB_SYSCALL, "sys_execv(): about to call enter_new_process.\n");
	enter_new_process(argc /*argc*/, 
					  argv /*userspace addr of argv*/,
			          stackptr, 
			          entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
