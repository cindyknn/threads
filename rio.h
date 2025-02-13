#ifndef RIO_H
#define RIO_H

#include <sys/types.h>
#include <stddef.h>

/****************************************
 * The RIO package - Robust I/O functions
 ****************************************/


/**
 * The RIO buffer state. This structure should not be accessed directly,
 * but rather should only be passed to the buffered rio functions.
 */
#define RIO_BUFSIZE 8192
struct rio {
    int rio_fd;                /* Descriptor for this internal buf */
    int rio_cnt;               /* Unread bytes in internal buf */
    char *rio_bufptr;          /* Next unread byte in internal buf */
    char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
};

/**
 * Robustly read n bytes (unbuffered).
 * 
 * @param fd     The file descriptor to read from.
 * @param usrbuf The buffer to read into.
 * @param n	     The number of bytes to read.
 * @return	     The number of bytes read, or -1 on error.
 */
ssize_t rio_readn(int fd, void *usrbuf, size_t n);

/**
 * Robustly write n bytes (unbuffered).
 * 
 * @param fd     The file descriptor to write to.
 * @param usrbuf The buffer to write from.
 * @param n	     The number of bytes to write.
 * @return	     The number of bytes written, or -1 on error.
 */
ssize_t rio_writen(int fd, void *usrbuf, size_t n);

/**
 * Associate a descriptor with a read buffer and reset buffer.
 * 
 * @param rp The rio state to initialize.
 * @param fd The file descriptor to associate with.
 */
void rio_readinitb(struct rio *rp, int fd); 

/**
 * Robustly read n bytes (buffered).
 * 
 * @param rp     The rio state.
 * @param usrbuf The buffer to read into.
 * @param n	     The number of bytes to read.
 * @return	     The number of bytes read, or -1 on error.
 */
ssize_t	rio_readnb(struct rio *rp, void *usrbuf, size_t n);

/**
 * Robustly read a text line (buffered). If the line is longer than maxlen, then
 * only maxlen bytes are written into usrbuf.
 * 
 * @param rp     The rio state.
 * @param usrbuf The buffer to read into.
 * @param maxlen The maximum number of bytes to read.
 * @return	     The number of bytes read, or -1 on error.
 */
ssize_t	rio_readlineb(struct rio *rp, void *usrbuf, size_t maxlen);

#endif