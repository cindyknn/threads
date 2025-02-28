/*
 * Cindy Nguyen (cn32)
 */

#define _GNU_SOURCE	// Enable the use of RTLD_NEXT

#include <sys/select.h>
#include <sys/time.h>

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#include "uthread_internal.h"

/**
 * This is the uthread stack size.
 */
#define	UTHR_STACK_SIZE	(1 << 21)

/**
 * These are the uthread stats.
 */
enum uthr_state { UTHR_FREE = 0, UTHR_RUNNABLE, UTHR_BLOCKED, UTHR_JOINING,
    UTHR_ZOMBIE };

/**
 * This is the maximum number of uthreads that a program can create.
 */
#define	NUTHR	64

static struct uthr {
	ucontext_t uctx;
	char *stack_base;
	void *(*start_routine)(void *restrict);
	void *restrict argp;
	void *ret_val;
	enum uthr_state state;
	struct uthr *prev;	// previous thread in the same queue
	struct uthr *next;	// next thread in the same queue
	struct uthr *joiner;
	bool detached;
	int fd;
	enum uthr_op op;
} uthr_array[NUTHR];

/**
 * These are the thread queues that are managed by the uthread scheduler.
 */
static struct uthr runq;
static struct uthr blockedq;
static struct uthr reapq;
static struct uthr freeq;

/**
 * This is the currently running thread.
 */
static struct uthr *curr_uthr;

/**
 * This is the context that executes the scheduler.
 */
static ucontext_t sched_uctx;

/**
 * This is the maximum duration that a thread is allowed to run before
 * it is preempted and moved to the tail of the run queue.
 */
static const struct itimerval quantum = { { .tv_sec = 0, .tv_usec = 100000 },
    { .tv_sec = 0, .tv_usec = 100000 } };

/**
 * This is a set of signals with only SIGPROF set.  It should not change after
 * its initialization.
 */
static sigset_t SIGPROF_set;

/**
 * These are pointers to the malloc and free functions from the standard C
 * library.
 */
static void *(*mallocp)(size_t);
static void (*freep)(void *);

static void uthr_scheduler(void);

/**
 * This function checks the current signal mask to ensure that the SIGPROF
 * signal is blocked.  If the signal is not blocked, the function will trigger
 * an assertion failure.
 *
 * This function will terminate the program if it fails to retrieve the current
 * signal mask using sigprocmask.
 */
static void
uthr_assert_SIGPROF_blocked(void)
{
	sigset_t old_set;

	if (sigprocmask(SIG_BLOCK, NULL, &old_set) == -1)
		uthr_exit_errno("sigprocmask");
	assert(sigismember(&old_set, SIGPROF));
}

void
uthr_block_SIGPROF(sigset_t *old_setp)
{
	// (Your code goes here.)
	sigprocmask(SIG_BLOCK, &SIGPROF_set, old_setp);

}

void
uthr_set_sigmask(const sigset_t *setp)
{
	// (Your code goes here.)
	sigprocmask(SIG_SETMASK, setp, NULL);
	///Preserves errno.
}

void *
uthr_intern_malloc(size_t size)
{
	uthr_assert_SIGPROF_blocked();
	return (mallocp(size));
}

void
uthr_intern_free(void *ptr)
{
	uthr_assert_SIGPROF_blocked();
	return (freep(ptr));
}

/**
 * Remove the given thread from its current queue.
 */
void 
uthr_removeq(struct uthr *td) {
	// printf("rem tryyyy: %ld\n", td - uthr_array);
	if (td->prev != NULL && td->next != NULL) {
		// printf("rem: %ld\n", td - uthr_array);
		(td->prev)->next = td->next;
		(td->next)->prev = td->prev;
		td->prev = NULL;
		td->next = NULL;
	}
}

/**
 * Insert the given thread to the end of the given queue.
 */
void 
uthr_insertq(struct uthr *td, struct uthr *queue) {
	// if (queue == &runq)
	// 	printf("insert runq: %ld\n", td - uthr_array);
	// td->prev = queue->prev;
	// td->next = queue;
	// (td->prev)->next = td;
	// queue->prev = td;

	struct uthr *old_tail = queue->prev;
	old_tail->next = td;
	queue->prev = td;
	td->prev = old_tail;
	td->next = queue;
}

/**
 * This function changes the state of the given thread to UTHR_ZOMBIE.  It
 * first ensures that SIGPROF is blocked and that the thread is currently
 * runnable.  The thread is then removed from the run queue and its state is
 * set to UTHR_ZOMBIE.  If the thread has a joiner, the joiner's state is set
 * to UTHR_RUNNABLE and it is inserted into the run queue.  If the thread is
 * detached and has no joiner, it is inserted into the reap queue.
 *
 * @param td Pointer to the thread to be transitioned to zombie state.
 */
static void
uthr_to_zombie(struct uthr *td)
{
	uthr_assert_SIGPROF_blocked();
	assert(td->state == UTHR_RUNNABLE);
	if (td->joiner != NULL) {
		assert(!td->detached);
                assert(td->joiner->state == UTHR_JOINING);
	}

	// (Your code goes here.)
	// Remove the thread from runq.
	// struct uthr *runq_prev = td->prev;
	// struct uthr *runq_next = td->next;
	// runq_prev->next = runq_next;
	// runq_next->prev = runq_prev;
	uthr_removeq(td);
	
	// Change the state to zombie.
	td->state = UTHR_ZOMBIE;
	if (td->joiner != NULL) {
		struct uthr *joiner = td->joiner;

		// printf("in joiner\n");
		joiner->state = UTHR_RUNNABLE;
		// Insert joiner into runq.
		// joiner->prev = runq.prev;
		// joiner->next = &runq;
		// (joiner->prev)->next = joiner;
		// runq.prev = joiner;
		uthr_insertq(joiner, &runq);
	} else if (td->detached) {
		// Insert thread into reapq.
		// td->prev = reapq.prev;
		// td->next = &reapq;
		// (td->prev)->next = td;
		// reapq.prev = td;
		uthr_insertq(td, &reapq);
	}
	
}

/**
 * Frees the resources associated with a thread and transitions it to the free
 * state.
 *
 * This function should be called when a thread has finished execution and its
 * resources need to be reclaimed.  It asserts that the SIGPROF signal is
 * blocked and that the thread is in the UTHR_ZOMBIE state before proceeding.
 *
 * @param td A pointer to the thread to be freed.
 */
static void
uthr_to_free(struct uthr *td)
{
	uthr_assert_SIGPROF_blocked();
	assert(td->state == UTHR_ZOMBIE);

	// (Your code goes here.)
	// Free thread's stack.
	uthr_intern_free(td->stack_base);
	td->stack_base = NULL;
	
	// Move thread from reapq to freeq.
	// struct uthr *reapq_prev = td->prev;
	// struct uthr *reapq_next = td->next;
	// reapq_prev->next = reapq_next;
	// reapq_next->prev = reapq_prev;
	// td->prev = freeq.prev;
	// td->next = &freeq;
	// (td->prev)->next = td;
	// freeq.prev = td;
	// uthr_removeq(td);
	uthr_insertq(td, &freeq);

	// Change thread's state to free.
	td->state = UTHR_FREE;

}

/**
 * This function is responsible for starting the execution of a user-level
 * thread.  It ensures that the SIGPROF signal is blocked, retrieves the
 * thread structure, and verifies that the thread is in a runnable state.  It
 * then unblocks the SIGPROF signal, calls the thread's start routine, saves
 * that routine's return value in the thread structure, and restores the
 * original signal mask.  Finally, it transitions the thread to a zombie
 * state.
 *
 * @param tidx The index of the thread to start.
 */
static void
uthr_start(int tidx)
{
	sigset_t old_set;

	uthr_assert_SIGPROF_blocked();
	struct uthr *td = &uthr_array[tidx];
	assert(td->state == UTHR_RUNNABLE);

	/*
	 * Before running the thread's start routine, SIGPROF signals must be
	 * unblocked so that the thread can be preempted if it runs for too
	 * long without blocking.
	 */
	if (sigprocmask(SIG_UNBLOCK, &SIGPROF_set, &old_set) == -1)
		uthr_exit_errno("sigprocmask");
	assert(sigismember(&old_set, SIGPROF));

	// (Your code goes here.)
	// Call the thread's start routine, saves return value in the thread structure.
	td->ret_val = td->start_routine(td->argp);
	
	// Restores the original signal mask.
	uthr_set_sigmask(&old_set);

	// Change state to zombie.
	//td->state = UTHR_ZOMBIE;
	
	pthread_exit(td->ret_val);
	// curr_uthr->ret_val = td->ret_val;
	// uthr_to_zombie(td);

	// Swap context back to scheduler.
	///swapcontext(&td->uctx, &sched_uctx);

}

int
pthread_create(pthread_t *restrict tidp, const pthread_attr_t *restrict attrp,
    void *(*start_routine)(void *restrict), void *restrict argp)
{
	// (Your code goes here.)
	sigset_t old_set;
	uthr_block_SIGPROF(&old_set);

	if (attrp != NULL) {
		uthr_set_sigmask(&old_set);
		return ENOTSUP;
	}
	
	//Take a thread from freeq and move it to runq.
	struct uthr *td = freeq.next;
	if (td == &freeq) {
		// printf("eag 1\n");
		uthr_set_sigmask(&old_set);
		return EAGAIN;
	}
	

	//Create stack_base, initialize ucontext, set start_routine & argp, state = Runnable, detached = 0.
	td->stack_base = uthr_intern_malloc(UTHR_STACK_SIZE);
	if (td->stack_base == NULL) {
		uthr_set_sigmask(&old_set);
		return EAGAIN;
	}

	// struct uthr *freeq_prev = td->prev;
	// struct uthr *freeq_next = td->next;
	// freeq_prev->next = freeq_next;
	// freeq_next->prev = freeq_prev;
	uthr_removeq(td);
	// td->prev = runq.prev;
	// td->next = &runq;
	// (td->prev)->next = td;
	// runq.prev = td;
	uthr_insertq(td, &runq);

	// ucontext_t new_uctx;

	getcontext(&td->uctx);
	td->uctx.uc_stack.ss_sp = td->stack_base;
	td->uctx.uc_stack.ss_size = UTHR_STACK_SIZE;
	// td->uctx.uc_link = &sched_uctx;
	makecontext(&td->uctx, (void(*)())uthr_start, 1, td - uthr_array);

	td->start_routine = start_routine;
	td->argp = argp;
	td->state = UTHR_RUNNABLE;
	td->joiner = NULL;
	td->detached = false;
	// td->ret_val = NULL;
	td->op = UTHR_OP_NOP;
	td->fd = -1;

	// Pass index of td in uthr_array to tidp.
	*tidp = td - uthr_array;

	uthr_set_sigmask(&old_set);

	// Call uthr_start.
	///uthr_start(td - uthr_array);
	///swapcontext(&curr_uthr->uctx, &new_uctx);

	return (0);
}

int
pthread_detach(pthread_t tid)
{
	sigset_t old_set;
	uthr_block_SIGPROF(&old_set);

	// (Your code goes here.)
	if (tid >= NUTHR || uthr_array[tid].state == UTHR_FREE) {
		uthr_set_sigmask(&old_set);
		return ESRCH;
	} else if (uthr_array[tid].detached) {
		uthr_set_sigmask(&old_set);
		return EINVAL;
	}

	// Change detached to true.
	uthr_array[tid].detached = true;

	uthr_set_sigmask(&old_set);

	return (0);
}

pthread_t
pthread_self(void)
{
	return (curr_uthr - uthr_array);
}

void
pthread_exit(void *retval)
{
	// (Your code goes here.)
	
	// Block SIGPROF.
	sigset_t old_set;
	uthr_block_SIGPROF(&old_set);

	// Set current thread's return value to input retval.
	///(void)retval;
	///retval = curr_uthr->ret_val;
	curr_uthr->ret_val = retval;

	// Changes current thread to zombie.
	uthr_to_zombie(curr_uthr);

	// Swap context back to scheduler.
	if (setcontext(&sched_uctx) == -1) {
		uthr_exit_errno("setcontext");
	}
	///swapcontext(&curr_uthr->uctx, &sched_uctx);

	///maybe put before swap
	///uthr_set_sigmask(&old_set);

	/*
	 * Since pthread_exit is declared as a function that never
	 * returns, the compiler requires us to ensure that the
	 * function's implementation never returns.
	 */
	for (;;);
}

int
pthread_join(pthread_t tid, void **retval)
{
	// (Your code goes here.)
	sigset_t old_set;
    uthr_block_SIGPROF(&old_set);

	//printf("b4 err\n");

	//|| uthr_array[tid].joiner == curr_uthr

	if (tid >= NUTHR) {
		//printf("err1\n");
		uthr_set_sigmask(&old_set);
		return ESRCH;
	}
	if (uthr_array[tid].state == UTHR_FREE) {
		//printf("err1\n");
		uthr_set_sigmask(&old_set);
		return ESRCH;
	}

	if (uthr_array[tid].detached) {
		//printf("err2\n");
		uthr_set_sigmask(&old_set);
		return EINVAL;
	}
	if (uthr_array[tid].joiner != NULL) {
		//printf("err2\n");
		uthr_set_sigmask(&old_set);
		return EINVAL;
	}

	if (&uthr_array[tid] == curr_uthr) {
		//printf("err3\n");
		uthr_set_sigmask(&old_set);
		return EDEADLK;
	}
	if ((uthr_array[tid].state == UTHR_JOINING) && (curr_uthr->joiner == &uthr_array[tid])) {
		//printf("err3\n");
		uthr_set_sigmask(&old_set);
		return EDEADLK;
	}

	//printf("no err\n");
		

	// 2 cases: tid's state is Zombie or not.	
	if (uthr_array[tid].state != UTHR_ZOMBIE) {	
		// printf("before swap\n");
		uthr_array[tid].joiner = curr_uthr;

		// Change state from Runnable to Joining. Remove curr_uthr from runq.
		curr_uthr->state = UTHR_JOINING;
		uthr_removeq(curr_uthr);

		// Swap context back to the scheduler.
		if (swapcontext(&curr_uthr->uctx, &sched_uctx) == -1) {
			uthr_exit_errno("swapcontext");
		}
	}
	//printf("mid\n");
	// if (uthr_array[tid].state == UTHR_ZOMBIE) {
		// printf("in zomb\n");
		// Save child thread's return value, and reap the child thread.
		// (void)retval;
		if (retval != NULL) {
			*retval = uthr_array[tid].ret_val;
			//printf("before retval\n");
			//printf("retval: %p\n", *retval);
		}
		uthr_to_free(&uthr_array[tid]);
	// }

	uthr_set_sigmask(&old_set);

	return (0);
}

int
sched_yield(void)
{
	// (Your code goes here.)
	
	// Causes the calling thread to relinquish the CPU. 
	// The thread is moved to the end of the queue for its static
	// priority and a new thread gets to run.

	sigset_t old_set;
    uthr_block_SIGPROF(&old_set);

	///use curr_uthr???
	///uthr_removeq(curr_uthr);
	///uthr_insertq(curr_uthr, &runq);

	// Swap context to the scheduler.
	if (swapcontext(&curr_uthr->uctx, &sched_uctx) == -1) {
		uthr_exit_errno("swapcontext");
	}

	uthr_set_sigmask(&old_set);

	return (0);
}

static struct fd_state {
	int maxfd;
	fd_set read_set;
	fd_set write_set;
	struct {
		int readers;
		int writers;
	} blocked[FD_SETSIZE];
} fd_state;

/**
 * Initializes the file descriptor state for the uthread library.
 *
 * This function sets the maximum file descriptor to -1, clears the read and
 * write file descriptor sets, and initializes the blocked readers and writers
 * count for each file descriptor to 0.
 */
static void
uthr_init_fd_state(void)
{
	fd_state.maxfd = -1;
	FD_ZERO(&fd_state.read_set);
	FD_ZERO(&fd_state.write_set);
	for (int i = 0; i < FD_SETSIZE; i++) {
		fd_state.blocked[i].readers = 0;
		fd_state.blocked[i].writers = 0;
	}
}

void
uthr_block_on_fd(int fd, enum uthr_op op)
{
	assert(fd < FD_SETSIZE);
	assert(op == UTHR_OP_READ || op == UTHR_OP_WRITE);

	// (Your code goes here.)

	sigset_t old_set;
    uthr_block_SIGPROF(&old_set);

	if (fd > fd_state.maxfd) {
		fd_state.maxfd = fd;
	}
	
	// Update calling thread's fd and op.
	///use curr_uthr???
	struct uthr *td = curr_uthr;
	td->fd = fd;
	td->op = op;

	// Move the thread to blockedq and update its state.
	uthr_removeq(td);
	uthr_insertq(td, &blockedq);
	td->state = UTHR_BLOCKED;

	if (td->op == UTHR_OP_READ) {
		// If op is read, update fd_state that a thread is now blocked reading on fd.
		if (fd_state.blocked[fd].readers == 0) {
			FD_SET(fd, &fd_state.read_set);			
		}
		fd_state.blocked[fd].readers += 1;
	} else {
		// If op is write, update fd_state that a thread is now blocked writing on fd.
		if (fd_state.blocked[fd].writers == 0) {
			FD_SET(fd, &fd_state.write_set);
		}
		fd_state.blocked[fd].writers += 1;
	}

	

	// Swap context back to scheduler.
	if (swapcontext(&curr_uthr->uctx, &sched_uctx) == -1) {
		uthr_exit_errno("swapcontext");
	}

	uthr_set_sigmask(&old_set);

}

/**
 * Checks and handles blocked threads.
 *
 * This function inspects the list of blocked threads and determines if any of
 * them can be moved to the runnable state based on the readiness of file
 * descriptors.  It uses the `select` system call to check the readiness of
 * file descriptors for reading or writing.
 *
 * @param wait If true, the function will wait indefinitely for file descriptors
 *             to become ready.  If false, the function will return immediately
 *             if no file descriptors are ready.
 */
static void
uthr_check_blocked(bool wait)
{
	struct timeval timeout = { 0, 0 }, *timeoutp = wait ? NULL : &timeout;

	uthr_assert_SIGPROF_blocked();

	// (Your code goes here.)
	// Copy file descriptor sets.
	fd_set ready_for_read = fd_state.read_set;
	fd_set ready_for_write = fd_state.write_set;
	
	// Will not block because of 0 timeout.
	
	int nready = select(fd_state.maxfd + 1, &ready_for_read, &ready_for_write, NULL, timeoutp);
	
	// If nready > 0, figure out which file descriptors are ready and unblock appropriate threads.
	// Note that a thread can only ever be blocked on one file descriptor at a time.
	struct uthr *head = &blockedq;
	struct uthr *curr = blockedq.next;
	if (nready < 0) {
		while(curr != head) {
			if (curr->state == UTHR_BLOCKED) {
				if ((FD_ISSET(curr->fd, &ready_for_read) && curr->op == UTHR_OP_READ) || (FD_ISSET(curr->fd, &ready_for_write) && curr->op == UTHR_OP_WRITE)) {
					// Change state, add to runq, remove from blockedq.
					curr->state = UTHR_RUNNABLE;
					struct uthr *blocked_prev = curr->prev;
					struct uthr *blocked_next = curr->next;
					blocked_prev->next = blocked_next;
					blocked_next->prev = blocked_prev;
					curr->prev = runq.prev;
					curr->next = &runq;
					(curr->prev)->next = curr;
					runq.prev = curr;
					///Inser & Remove!
					
					// Decrement readers/writers, if that int gets to 0 then FD_CLR that fd from read_set/write_set.
					if(curr->op == UTHR_OP_READ) {
						fd_state.blocked[curr->fd].readers -= 1;
						if(fd_state.blocked[curr->fd].readers == 0) {
							FD_CLR(curr->fd, &fd_state.read_set);
						}
					} else if (curr->op == UTHR_OP_WRITE) {
						fd_state.blocked[curr->fd].writers -= 1;
						if(fd_state.blocked[curr->fd].writers == 0) {
							FD_CLR(curr->fd, &fd_state.write_set);
						}
					}

					curr = blocked_next;

				} else {
					curr = curr->next;
				}
			}
		}
	}			

}

/**
 * Scheduler function for user-level threads.
 *
 * This function is responsible for managing the execution of user-level
 * threads.  It continuously checks for runnable threads and context switches
 * to them.  If a thread is preempted, it is moved to the tail of the run
 * queue.  If no threads are runnable, it waits for a blocked thread to become
 * ready.  It also reaps and frees detached threads that have finished
 * execution.
 *
 * This function runs in an infinite loop and does not return.
 */
static void
uthr_scheduler(void)
{
	uthr_assert_SIGPROF_blocked();
	for (;;) {

		// struct uthr *runq_curr = runq.next;
		// for (int i = 0; i < 5; i++) {
		// 	if (runq_curr == NULL) {
		// 		printf("runq curr is null: %ld\n", runq_curr - uthr_array);
		// 	}
		// 	struct uthr *runq_next = runq_curr->next;
		// 	printf("in runq: %ld\n", runq_curr - uthr_array);
		// 	runq_curr = runq_next;
		// }

		// printf("before if: %d\n", uthr_array[1].state);
		// (Your code goes here.)
		// Check if runq is not empty.
		if (runq.next != &runq) {
			// Reset the preemption timer.
			if (setitimer(ITIMER_PROF, &quantum, NULL) < 0) {
				uthr_exit_errno("setitimer");
			}
			
			// Set curr_uthr to the first thread in runq. Run that thread by resuming its ucontext.
			///struct uthr *old_uthr = &curr_uthr;
			
			// printf("running %ld\n", curr_uthr - uthr_array);
			curr_uthr = runq.next;
			/// uthr_removeq(curr_uthr);

			/// if ((curr_uthr - uthr_array) != 0)
			/// 	printf("running %ld\n", curr_uthr - uthr_array);
			
			if (swapcontext(&sched_uctx, &curr_uthr->uctx) < 0) {
				uthr_exit_errno("swapcontext");
			}

			// sigset_t old_set;

			// if (sigismember(&old_set, SIGPROF)) {
			// 	printf("yes\n");
			// } else {
			// 	printf("no\n");
			// }

    		// uthr_block_SIGPROF(&old_set);
			// printf("aft swap\n");

			// When scheduler is resumed, check if curr_uthr is still in Runnable state.
			if (curr_uthr->state == UTHR_RUNNABLE) {
				// Move unblocked threads to runq, and move curr_uthr to end of runq.
				uthr_check_blocked(false);
				uthr_removeq(curr_uthr);
				uthr_insertq(curr_uthr, &runq);
				// printf("runna\n");
			}

			// printf("aft runna\n");

			///uthr_set_sigmask(&old_set);

		} else {
			// If runq is empty, wait for a Blocked thread to become Runnable.
			// printf("before cb true\n");
			uthr_check_blocked(true);
			// printf("aft cb true\n");
		}
		
		
		// printf("before while\n");

		// Free all threads in reapq.
		struct uthr *reapq_curr = reapq.next;
		while (reapq_curr != &reapq) {
			struct uthr *reapq_next = reapq_curr->next;
			uthr_removeq(reapq_curr);
			uthr_to_free(reapq_curr);
			reapq_curr = reapq_next;
		}

		// printf("aft while\n");

	}

	///a lot of swap_context, some make_context? Don't have to get&make again after
}

/**
 * Timer (SIGPROF) signal handler for user-level threads.
 *
 * This function is called when a timer signal is received.  It saves the
 * current errno value, performs a context switch from the current user-level
 * thread to the scheduler context, and then restores the errno value.
 *
 * @param signum The signal number.
 */
static void
uthr_timer_handler(int signum)
{
	(void)signum;
	int save_errno = errno;
	if (swapcontext(&curr_uthr->uctx, &sched_uctx) == -1)
		uthr_exit_errno("swapcontext");
	errno = save_errno;
}

/**
 * @brief Initializes the user-level threading library.
 *
 * This function is automatically called before the main function is executed,
 * due to the __attribute__((constructor)) attribute.  It performs the following
 * initialization tasks:
 * 
 * - Initializes the SIGPROF signal set.
 * - Looks up the symbols for malloc and free functions.
 * - Sets up the scheduler context.
 * - Initializes the currently running thread.
 * - Initializes the queue of free threads.
 * - Initializes the file descriptor state.
 * - Sets up the SIGPROF signal handler.
 * - Configures the interval timer for thread scheduling.
 *
 * If any of the initialization steps fail, the function will call
 * uthr_exit_errno() to terminate the program with an error message.
 */
__attribute__((constructor))
static void
uthr_init(void)
{
	static char sched_stack[UTHR_STACK_SIZE];

	/*
	 * SIGPROF_set must be initialized before calling uthr_lookup_symbol().
	 */
	// (Your code goes here.)
	if (sigemptyset(&SIGPROF_set) == -1)
		uthr_exit_errno("sigempty");
	if (sigaddset(&SIGPROF_set, SIGPROF) == -1)
		uthr_exit_errno("sigaddset");

	// Block SIGPROF signals.
	sigset_t old_set;
	uthr_block_SIGPROF(&old_set);

	uthr_lookup_symbol((void *)&mallocp, "malloc");
	uthr_lookup_symbol((void *)&freep, "free");

	/*
	 * Initialize the scheduler context using the above sched_stack.
	 */
	// (Your code goes here.)
	getcontext(&sched_uctx);
	sched_uctx.uc_stack.ss_sp = sched_stack;
	sched_uctx.uc_stack.ss_size = UTHR_STACK_SIZE;
	sched_uctx.uc_link = NULL;
	sched_uctx.uc_sigmask = SIGPROF_set;
	makecontext(&sched_uctx, (void(*)())uthr_scheduler, 0);

	// Note: For all 4 queues, set prev and next struct entries to itself.
	runq.prev = &runq;
	runq.next = &runq;
	blockedq.prev = &blockedq;
	blockedq.next = &blockedq;
	reapq.prev = &reapq;
	reapq.next = &reapq;
	freeq.prev = &freeq;
	freeq.next = &freeq;

	/*
	 * Initialize the currently running thread and insert it at the tail
	 * of the run queue.
	 */
	curr_uthr = &uthr_array[0];
	curr_uthr->state = UTHR_RUNNABLE;
	// (Your code goes here.)
	uthr_insertq(curr_uthr, &runq);	

	/*
	 * Initialize the queue of free threads.  Skip zero, which is already
	 * running.
	 */
	// (Your code goes here.)
	for (int i = 1; i < NUTHR; i++) {
		uthr_insertq(&uthr_array[i], &freeq);
		uthr_array[i].state = UTHR_FREE;
	}
	 
	uthr_init_fd_state();

	/*
	 * Set up the SIGPROF signal handler.
	 */
	// (Your code goes here.)
	///create a new struct sigaction???
	struct sigaction action;
	action.sa_handler = uthr_timer_handler;
	action.sa_flags = SA_RESTART;
	action.sa_mask = SIGPROF_set;
	// if (sigemptyset(&action.sa_mask) < 0)
	// 	uthr_exit_errno("sigemptyset error");
	///need to block any other signals? sigaddset(&action.sa_mask, SIGCHLD)
	if (sigaction(SIGPROF, &action, NULL) < 0)
		uthr_exit_errno("sigaction error");
	
	/*
	 * Configure the interval timer for thread scheduling.
	 */
	// (Your code goes here.)
	///anything else??? Error check
	setitimer(ITIMER_PROF, &quantum, NULL);


	///setcontext(&sched_uctx);

	uthr_set_sigmask(&old_set);

}

void
uthr_lookup_symbol(void **addrp, const char *symbol)
{
	/*
	 * Block preemption because dlerror() may use a static buffer.
	 */
	// (Your code goes here.)
	sigset_t old_set;
	uthr_block_SIGPROF(&old_set);

	/*
	 * A concurrent lookup by another thread may have already set the
	 * address.
	 */
	if (*addrp == NULL) {
		/*
		 * See the manual page for dlopen().
		 */
		// (Your code goes here.)
		
		///dlsym and dlerror to look up and return the specified symbol’s address.
		dlerror();
		*addrp = dlsym(RTLD_NEXT, symbol);
		char *error = dlerror();
		if (error != NULL) {
			uthr_exit_errno(error);
		}
	}

	// (Your code goes here.)
	uthr_set_sigmask(&old_set);
}

void
uthr_exit_errno(const char *message)
{
	perror(message);
	exit(EXIT_FAILURE);
}
