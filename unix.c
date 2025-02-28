/**
 * This file provides wrappers for unix I/O functions.
 */

#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#include "uthread_internal.h"

/**
 * Sets the file descriptor to non-blocking mode.
 *
 * @param fd The file descriptor to set to non-blocking mode.
 */
static void
unix_set_fd_NONBLOCK(int fd)
{
	// (Your code goes here.)
	// Sets fd to non-blocking mode.
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

/**
 * Creates an endpoint for communication and sets it to non-blocking mode.
 *
 * Sets errno on an error.
 *
 * @param domain The communication domain.
 * @param type The communication semantics.
 * @param protocol The protocol to be used.
 * @return The file descriptor for the new socket, or -1 on error.
 */
int
socket(int domain, int type, int protocol)
{
	static int (*socketp)(int, int, int);
	int s;

	if (socketp == NULL)
		uthr_lookup_symbol((void *)&socketp, "socket");
	s = socketp(domain, type, protocol);
	if (s != -1) {
		unix_set_fd_NONBLOCK(s);
	}

	return (s);
}

/**
 * Accepts a connection on a new socket and sets the new socket to non-blocking
 * mode.
 *
 * Sets errno on an error.
 *
 * @param s The file descriptor of the socket.
 * @param addr The address of the connecting entity.
 * @param addrlen The length of the address.
 * @return The file descriptor for the accepted socket, or -1 on error.
 */
int
accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
	static int (*acceptp)(int, struct sockaddr *, socklen_t *);
	int s_conn;

	// (Your code goes here.)

	// Lookup the symbol for accept.
	if (acceptp == NULL)
		uthr_lookup_symbol((void *)&acceptp, "accept");
	unix_set_fd_NONBLOCK(s);

	// Keep looping until a connection is accepted.
	for (;;) {
		int save_errno = errno;
		s_conn = acceptp(s, addr, addrlen);
		// Check if a connection is accepted.
		if (s_conn < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				// Block on fd for read operation.
				errno = save_errno;
				uthr_block_on_fd(s, UTHR_OP_READ);
			} else {
				return -1;
			}
		} else {
			// Break out of the infinite loop.
			break;
		}
	}
	// Set the new socket to non-blocking mode.
	if (s_conn != -1)
		unix_set_fd_NONBLOCK(s_conn);

	return (s_conn);
}

/**
 * Reads data from a file descriptor.
 *
 * Sets errno on an error.
 *
 * @param fd The file descriptor to read from.
 * @param buf The buffer to store the read data.
 * @param count The number of bytes to read.
 * @return The number of bytes read, or -1 on error.
 */
ssize_t
read(int fd, void *buf, size_t count)
{
	static int (*readp)(int, void *, size_t);
	int rc;

	// (Your code goes here.)

	// Lookup the symbol for read.
	if (readp == NULL)
		uthr_lookup_symbol((void *)&readp, "read");

	// Set the fd to non-blocking mode.
	unix_set_fd_NONBLOCK(fd);

	int save_errno = errno;
	rc = readp(fd, buf, count);
	// Check if read is successful.
	if (rc < 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			// Block on fd for read operation.
			errno = save_errno;
			uthr_block_on_fd(fd, UTHR_OP_READ);
		} else {
			return -1;
		}
	}
	return (rc);
}

/**
 * Writes data to a file descriptor.
 *
 * Sets errno on an error.
 *
 * @param fd The file descriptor to write to.
 * @param buf The buffer containing the data to write.
 * @param count The number of bytes to write.
 * @return The number of bytes written, or -1 on error.
 */
ssize_t
write(int fd, const void *buf, size_t count)
{
	static int (*writep)(int, const void *, size_t);
	int rc;

	// (Your code goes here.)

	// Lookup the symbol for read.
	if (writep == NULL)
		uthr_lookup_symbol((void *)&writep, "write");

	// Set the fd to non-blocking mode.
	unix_set_fd_NONBLOCK(fd);

	int save_errno = errno;
	rc = writep(fd, buf, count);
	// Check if write is successful.
	if (rc < 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			// Block on fd for write operation.
			errno = save_errno;
			uthr_block_on_fd(fd, UTHR_OP_WRITE);
		} else {
			return -1;
		}
	}
	return (rc);
}