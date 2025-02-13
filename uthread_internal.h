#ifndef UTHREAD_INTERNAL_H
#define	UTHREAD_INTERNAL_H

/**
 * The following functions are for the internal use of the uthread library.
 */

#include <sys/types.h>

#include <signal.h>

enum uthr_op { UTHR_OP_NOP = 0, UTHR_OP_READ, UTHR_OP_WRITE };

/**
 * Blocks the calling thread until the specified operation on the specified
 * file descriptor can complete.
 *
 * @param fd The file descriptor.
 * @param op The operation on the file descriptor (read or write).
 */
void uthr_block_on_fd(int fd, enum uthr_op op);

/**
 * Blocks the SIGPROF signal and saves the old signal set.
 *
 * @param old_setp Pointer to the old signal set.
 */
void uthr_block_SIGPROF(sigset_t *old_setp);

/**
 * Exits the process with an error message that is based in part on the
 * current value of errno.
 *
 * @param message The error message to print.
 */
void uthr_exit_errno(const char *message);

/**
 * Frees the memory block pointed to by ptr.
 *
 * Requires that SIGPROF signals are blocked.
 *
 * @param ptr A pointer to the memory block to free.
 */
void uthr_intern_free(void *ptr);

/**
 * Allocates memory of the specified size.
 *
 * Sets errno if the allocation fails.
 *
 * Requires that SIGPROF signals are blocked.
 *
 * @param size The size of the memory block to allocate.
 * @return A pointer to the allocated memory, or NULL if the allocation fails.
 */
void *uthr_intern_malloc(size_t size);

/**
 * Looks up the address of the specified symbol.
 *
 * @param addrp Pointer to store the address of the symbol.
 * @param symbol The name of the symbol to look up.
 */
void uthr_lookup_symbol(void **addrp, const char *symbol);

/**
 * Sets the signal mask to the specified set.
 *
 * Preserves the current value of errno.
 *
 * @param setp Pointer to the signal set to apply.
 */
void uthr_set_sigmask(const sigset_t *setp);

#endif
