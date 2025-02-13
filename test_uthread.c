#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Test thread functions */
void *
thread_function_null(void *arg)
{
	(void)arg;
	return (NULL);
}

void *
thread_function_sleep(void *arg)
{
	(void)arg;
	sleep(1);
	return (NULL);
}

void *
thread_function_return(void *arg)
{
	return (arg);
}

void *
thread_function_exit(void *arg)
{
	pthread_exit(arg);
}

void *
thread_function_join(void *arg)
{
	pthread_t *thread = arg;
	int	  *retval = malloc(sizeof(int));
	*retval = pthread_join(*thread, NULL);
	return (retval);
}

/**
 * Test cases for pthread_create.
 */

/**
 * Test invalid settings in attr.
 */
void
test_pthread_create_invalidattr(void)
{
	pthread_t      thread;
	pthread_attr_t attr;
	int	       retval;

	printf("TEST pthread_create - invalid settings in attr: ");

	pthread_attr_init(&attr);

	retval = pthread_create(&thread, &attr, thread_function_null, NULL);
	if (retval == ENOTSUP) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", ENOTSUP, retval);
	}
}

/**
 * Test exceeding limit on number of threads.
 */
void
test_pthread_create_threadlimit(void)
{
	pthread_t threads[63];
	pthread_t thread;
	int	  retval;

	printf("TEST pthread_create - exceeded thread limit: ");

	for (int i = 0; i < 63; i++) {
		if (pthread_create(&threads[i], NULL, thread_function_null,
			NULL) != 0) {
			printf("Unexpected error in pthread_create\n");
			return;
		}
	}

	retval = pthread_create(&thread, NULL, thread_function_null, NULL);
	if (retval == EAGAIN) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", EAGAIN, retval);
	}

	// Clean up
	for (int i = 0; i < 63; i++) {
		pthread_join(threads[i], NULL);
	}
}

/**
 * Test cases for pthread_detach.
 */

/**
 * Test the case where the caller specifies an invalid thread indentifier.
 */
void
test_pthread_detach_invalid(void)
{
	pthread_t thread;
	int	  retval;

	printf("TEST pthread_detach - invalid thread identifier: ");

	thread = -1;

	retval = pthread_detach(thread);
	if (retval == ESRCH) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", ESRCH, retval);
	}
}

/**
 * Test the case where the specified thread has already terminated.
 */
void
test_pthread_detach_terminated(void)
{
	pthread_t thread;
	int	  retval;

	printf("TEST pthread_detach - thread has terminated: ");

	if (pthread_create(&thread, NULL, thread_function_null, NULL) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	if (pthread_join(thread, NULL) != 0) {
		printf("Unexpected error in pthread_join\n");
		return;
	}

	retval = pthread_detach(thread);
	if (retval == ESRCH) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", ESRCH, retval);
	}
}

/**
 * Test the case where the specified thread has already been detached.
 */
void
test_pthread_detach_detached(void)
{
	pthread_t thread;
	int	  retval;

	printf("TEST pthread_detach - thread has been detached: ");

	if (pthread_create(&thread, NULL, thread_function_sleep, NULL) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	if (pthread_detach(thread) != 0) {
		printf("Unexpected error in pthread_detach\n");
		return;
	}

	retval = pthread_detach(thread);
	if (retval == EINVAL) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", EINVAL, retval);
	}
}

/**
 * Test the case where the specified thread is already a zombie.
 */
void
test_pthread_detach_zombie(void)
{
	pthread_t thread;
	int	  retval;

	printf("TEST pthread_detach - thread is already a zombie: ");

	if (pthread_create(&thread, NULL, thread_function_null, NULL) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	// Yield to the above created thread, which will immediately return.
	(void)sched_yield();

	retval = pthread_detach(thread);
	if (retval == 0) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", 0, retval);
	}
}

/**
 * Test cases for pthread_join.
 */

/**
 * Test the case where the caller specifies an invalid thread identifier.
 */
void
test_pthread_join_invalid(void)
{
	pthread_t thread;
	int	  retval;

	printf("TEST pthread_join - invalid thread identifier: ");

	thread = -1;

	retval = pthread_join(thread, NULL);
	if (retval == ESRCH) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", ESRCH, retval);
	}
}

/**
 * Test the case where the thread has already terminated.
 */
void
test_pthread_join_terminated(void)
{
	pthread_t thread;
	int	  retval;

	printf("TEST pthread_join - thread has terminated: ");

	if (pthread_create(&thread, NULL, thread_function_null, NULL) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	if (pthread_join(thread, NULL) != 0) {
		printf("Unexpected error in pthread_join\n");
		return;
	}

	retval = pthread_join(thread, NULL);
	if (retval == ESRCH) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", ESRCH, retval);
	}
}

/**
 * Test the case where the calling thread specifies itself.
 */
void
test_pthread_join_self(void)
{
	pthread_t thread;
	int	  retval;

	printf("TEST pthread_join - thread specifies calling thread: ");

	thread = pthread_self();

	retval = pthread_join(thread, NULL);
	if (retval == EDEADLK) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", EDEADLK, retval);
	}
}

/**
 * Test the case where the target thread is already waiting to join with the
 * calling thread.
 */
void
test_pthread_join_circular(void)
{
	pthread_t thread, self;
	int	  retval1;
	int	 *retval2;

	printf(
	    "TEST pthread_join - thread is already joining with calling thread: ");

	self = pthread_self();
	if (pthread_create(&thread, NULL, thread_function_join, &self) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	retval1 = pthread_join(thread, (void **)&retval2);
	if ((retval1 == 0 && *retval2 == EDEADLK) ||
	    (retval1 == EDEADLK && *retval2 == 0)) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected 0 and %d, got %d and %d\n", EDEADLK,
		    retval1, *retval2);
	}

	free(retval2);
}

/**
 * Test the case where the thread is detached.
 */
void
test_pthread_join_detached(void)
{
	pthread_t thread;
	int	  retval;

	printf("TEST pthread_join - thread has been detached: ");

	if (pthread_create(&thread, NULL, thread_function_sleep, NULL) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	if (pthread_detach(thread) != 0) {
		printf("Unexpected error in pthread_detach\n");
		return;
	}

	retval = pthread_join(thread, NULL);
	if (retval == EINVAL) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %d, got %d\n", EINVAL, retval);
	}
}

/**
 * Test the case where another thread is already waiting to join with the
 * target thread.
 */
void
test_pthread_join_conflict(void)
{
	pthread_t thread, target;
	int	  retval1;
	int	 *retval2;

	printf(
	    "TEST pthread_join - thread is already joining with target thread: ");

	if (pthread_create(&target, NULL, thread_function_sleep, NULL) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	if (pthread_create(&thread, NULL, thread_function_join, &target) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	retval1 = pthread_join(target, NULL);
	if (pthread_join(thread, (void **)&retval2) != 0) {
		printf("Unexpected error in pthread_join\n");
		return;
	}

	if ((retval1 == 0 && *retval2 == EINVAL) ||
	    (retval1 == EINVAL && *retval2 == 0)) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected 0 and %d, got %d and %d\n", EINVAL,
		    retval1, *retval2);
	}

	free(retval2);
}

/**
 * Test cases for returning a value from the start routine and pthread_exit.
 */

/**
 * Test the return value from the start routine.
 */
void
test_pthread_join_return(void)
{
	pthread_t thread;
	void	 *arg = (void *)0xDEADBEEFDEADBEEF, *arg_ret;

	printf("TEST pthread_join - return value is set correctly: ");

	if (pthread_create(&thread, NULL, thread_function_return, arg) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	if (pthread_join(thread, &arg_ret) != 0) {
		printf("Unexpected error in pthread_join\n");
		return;
	}

	if (arg == arg_ret) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %p, got %p\n", arg, arg_ret);
	}
}

/**
 * Test the value passed to pthread_exit.
 */
void
test_pthread_exit_return(void)
{
	pthread_t thread;
	void	 *arg = (void *)0xDEADBEEFDEADBEEF, *arg_ret;

	printf("TEST pthread_exit - return value is set correctly: ");

	if (pthread_create(&thread, NULL, thread_function_exit, arg) != 0) {
		printf("Unexpected error in pthread_create\n");
		return;
	}

	if (pthread_join(thread, &arg_ret) != 0) {
		printf("Unexpected error in pthread_join\n");
		return;
	}

	if (arg == arg_ret) {
		printf("PASSED\n");
	} else {
		printf("FAILED: expected %p, got %p\n", arg, arg_ret);
	}
}

int
main(void)
{
	test_pthread_create_invalidattr();
	test_pthread_create_threadlimit();
	printf("\n");

	test_pthread_detach_invalid();
	test_pthread_detach_terminated();
	test_pthread_detach_detached();
	test_pthread_detach_zombie();
	printf("\n");

	test_pthread_join_invalid();
	test_pthread_join_terminated();
	test_pthread_join_self();
	test_pthread_join_circular();
	test_pthread_join_detached();
	test_pthread_join_conflict();
	test_pthread_join_return();
	printf("\n");

	test_pthread_exit_return();
}
