/**
 * This file provides wrappers for stdlib functions.
 */

#include <signal.h>
#include <stdlib.h>

#include "uthread_internal.h"

/**
 * Allocates memory of the specified size.
 *
 * Sets errno if the allocation fails.
 *
 * @param size The size of the memory block to allocate.
 * @return A pointer to the allocated memory, or NULL if the allocation fails.
 */
void *
malloc(size_t size)
{
	sigset_t old_set;

	uthr_block_SIGPROF(&old_set);
	void *ptr = uthr_intern_malloc(size);
	uthr_set_sigmask(&old_set);
	return (ptr);
}

/**
 * Frees the memory block pointed to by ptr.
 *
 * @param ptr A pointer to the memory block to free.
 */
void
free(void *ptr)
{
	sigset_t old_set;

	uthr_block_SIGPROF(&old_set);
	uthr_intern_free(ptr);
	uthr_set_sigmask(&old_set);
}

/**
 * Allocates memory for an array of nmemb elements of size bytes each and
 * initializes all bytes in the allocated storage to zero.
 *
 * Sets errno if the allocation fails.
 *
 * @param nmemb The number of elements to allocate.
 * @param size The size of each element.
 * @return A pointer to the allocated memory, or NULL if the allocation fails.
 */
void *
calloc(size_t nmemb, size_t size)
{
	static void *(*callocp)(size_t, size_t);
	sigset_t old_set;

	if (callocp == NULL)
		uthr_lookup_symbol((void *)&callocp, "calloc");
	uthr_block_SIGPROF(&old_set);
	void *ptr = callocp(nmemb, size);
	uthr_set_sigmask(&old_set);
	return (ptr);
}

/**
 * Changes the size of the memory block pointed to by ptr to size bytes.
 *
 * Sets errno if the allocation fails.
 *
 * @param ptr A pointer to the memory block to resize.
 * @param size The new size of the memory block.
 * @return A pointer to the resized memory block, or NULL if the allocation
 *         fails.
 */
void *
realloc(void *ptr, size_t size)
{
	static void *(*reallocp)(void *, size_t);
	sigset_t old_set;

	if (reallocp == NULL)
		uthr_lookup_symbol((void *)&reallocp, "realloc");
	uthr_block_SIGPROF(&old_set);
	void *new_ptr = reallocp(ptr, size);
	uthr_set_sigmask(&old_set);
	return (new_ptr);
}
