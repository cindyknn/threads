/*
 * A simple, multithreaded HTTP/1.0 Web server that uses the GET method
 * to serve static content.  Derived from the B&O textbook.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rio.h"

#define	MAXLINE	 8192  /* Max text line length */
#define LISTENQ  1024  /* Second argument to listen() */

static void client_error(int fd, const char *cause, const char *errnum,
    const char *shortmsg, const char *longmsg);
static void doit(int fd);
static const char *get_filetype(const char *filename);
static void parse_uri(const char *uri, char *filename);
static void *thread_start(void *arg);

static bool verbose;

/**
 * Opens a listening socket on the specified TCP port number.
 *
 * @param port	The TCP port number.
 * @return     A file descriptor for the listening socket on success.
 *             Otherwise, -2 for getaddrinfo error or -1 with errno set
 *             for other errors.
 */
static int
open_listenfd(char *port)
{
	struct addrinfo hints, *listp, *p;
	int listenfd, optval = 1, rc;

	// Get a list of potential server addresses.
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM;             // Accept connections
	hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; // ... on any IP address
	hints.ai_flags |= AI_NUMERICSERV;            // ... using port number
	if ((rc = getaddrinfo(NULL, port, &hints, &listp)) != 0) {
		warnx("getaddrinfo failed (port %s): %s",
		    port, gai_strerror(rc));
		return (-2);
	}

	// Walk the list for one that we can bind to.
	for (p = listp; p; p = p->ai_next) {
		// Create a socket descriptor.
		if ((listenfd = socket(p->ai_family, p->ai_socktype,
		    p->ai_protocol)) < 0)
			continue;  // Socket failed, try the next.

		// Eliminates "Address already in use" error from bind.
		setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
		    (const void *)&optval, sizeof(int));

		// Bind the descriptor to the address.
		if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
			break; // Success.
		if (close(listenfd) < 0) // Bind failed, try the next.
			return (-1);
	}

	// Clean up.
	freeaddrinfo(listp);
	if (p == NULL) // No address worked.
		return (-1);

	/*
	 * Use listen() to ready the socket for accepting connection requests.
	 * Set the backlog to LISTENQ.
	 */
	if (listen(listenfd, LISTENQ) < 0) {
		close(listenfd);
		return (-1);
	}
	return (listenfd);
}

int
main(int argc, char **argv)
{
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	pthread_t thread;
	int *connfd, listenfd, opt, rc;
	char hostname[MAXLINE], port[MAXLINE];

	// Check command line args.
	while ((opt = getopt(argc, argv, "v")) != -1) {
		switch (opt) {
		case 'v':
			verbose = true;
			break;
		default: /* '?' */
			errx(EXIT_FAILURE, "usage: %s [-v] <port>", argv[0]);
		}
	}
	if (argc != optind + 1)
		errx(EXIT_FAILURE, "usage: %s [-v] <port>", argv[0]);
	listenfd = open_listenfd(argv[optind]);
	if (listenfd == -1)
		err(EXIT_FAILURE, "open_listenfd");
	else if (listenfd == -2) {
		exit(EXIT_FAILURE);
	}
	while (true) {
		if ((connfd = malloc(sizeof(*connfd))) == NULL)
			err(EXIT_FAILURE, "malloc");
		clientlen = sizeof(clientaddr);
		if ((*connfd = accept(listenfd, (struct sockaddr *)&clientaddr,
		    &clientlen)) == -1) {
		        perror("accept");
			free(connfd);
			continue;
		}
		if (verbose) {
			if ((rc = getnameinfo((struct sockaddr *)&clientaddr,
			    clientlen, hostname, MAXLINE, port, MAXLINE, 0)) !=
			    0) {
				fprintf(stderr, "getnameinfo: %s\n",
				    gai_strerror(rc));
				if (close(*connfd) == -1)
					perror("close");
				free(connfd);
				continue;
			}
			printf("Accepted connection from (%s, %s)\n", hostname,
			    port);
		}
		if ((rc = pthread_create(&thread, NULL, thread_start,
		    connfd)) != 0) {
			warnx("pthread_create: %s", strerror(rc));
			if (close(*connfd) == -1)
				perror("close");
			free(connfd);
		}
	}
}

/**
 * The start routine for the thread that handles an HTTP request/response
 * transaction.
 *
 * @param arg A pointer to the file descriptor for the client connection.
 */
static void *
thread_start(void *arg)
{
	int connfd = *(int *)arg, rc;

	free(arg);
	if ((rc = pthread_detach(pthread_self())) != 0)
		errx(EXIT_FAILURE, "pthread_detach: %s", strerror(rc));
	doit(connfd);
	if (close(connfd) == -1)
		perror("close");
	return (NULL);
}

/**
 * Handle one HTTP request/response transaction.
 *
 * @param fd The file descriptor for the client connection.
 */
static void
doit(int fd)
{
	struct rio rio;
	struct stat sbuf;
	ssize_t rc;
	int count, srcfd;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], *srcp;
	const char *filetype;

	/**
	 * Read request line and headers.  Extract the URI from the request
	 * line.  Discard the header lines.
	 */
	rio_readinitb(&rio, fd);
	if ((rc = rio_readlineb(&rio, buf, MAXLINE)) == -1) {
		perror("rio_readlineb");
		return;
	} else if (rc == 0)
		return;
	if (verbose)
		fputs(buf, stdout);
	sscanf(buf, "%s %s %s", method, uri, version);
	if (strcasecmp(method, "GET")) {
		client_error(fd, method, "501", "Not Implemented",
		    "Tiny does not implement this method");
		return;
	}
	if ((rc = rio_readlineb(&rio, buf, MAXLINE)) == -1) {
		perror("rio_readlineb");
		return;
	} else if (rc == 0)
		return;
	if (verbose)
		fputs(buf, stdout);
	while (strcmp(buf, "\r\n") != 0) {
		if ((rc = rio_readlineb(&rio, buf, MAXLINE)) == -1) {
			perror("rio_readlineb");
			return;
		} else if (rc == 0)
			return;
		if (verbose)
			fputs(buf, stdout);
	}

	// Parse URI from GET request.
	parse_uri(uri, filename);

	// Map the file if it exists and is "world" readable.
	if ((srcfd = open(filename, O_RDONLY, 0)) == -1) {
		client_error(fd, filename, "404", "Not found",
		    "Tiny couldn't find this file");
		return;
	}
	if (fstat(srcfd, &sbuf) == -1)
		err(EXIT_FAILURE, "fstat");
	if (!S_ISREG(sbuf.st_mode) || (S_IROTH & sbuf.st_mode) == 0) {
		client_error(fd, filename, "403", "Forbidden",
		    "Tiny couldn't read the file");
		if (close(srcfd) == -1)
			err(EXIT_FAILURE, "close");
		return;
	}
	if ((srcp = mmap(0, sbuf.st_size, PROT_READ, MAP_PRIVATE, srcfd, 0)) ==
	    MAP_FAILED)
		err(EXIT_FAILURE, "mmap");
	if (close(srcfd) == -1)
		err(EXIT_FAILURE, "close");

	// Send response headers to client.
	filetype = get_filetype(filename);
	count = snprintf(buf, MAXLINE,
	    "HTTP/1.0 200 OK\r\n"
	    "Server: Tiny Web Server\r\n"
	    "Content-length: %jd\r\n"
	    "Content-type: %s\r\n\r\n",
	    (intmax_t)sbuf.st_size, filetype);
	assert(count < MAXLINE);
	if (rio_writen(fd, buf, count) != count) {
		perror("rio_writen");
		return;
	}

	// Send response body to client.
	if (rio_writen(fd, srcp, sbuf.st_size) != sbuf.st_size) {
		perror("rio_writen");
		return;
	}

	// Unmap the file.
	if (munmap(srcp, sbuf.st_size) == -1)
		err(EXIT_FAILURE, "munmap");
}

/*
 * Parse URI into file name.
 *
 * @param uri The URI.
 * @param filename A pointer to a character array that is sufficiently
 *            large to hold the file name.
 */
static void
parse_uri(const char *uri, char *filename)
{
	strcpy(filename, ".");
	strcat(filename, uri);
	if (uri[strlen(uri) - 1] == '/')
		strcat(filename, "index.html");
}

/*
 * Derive file type from file name.
 *
 * @param filename The file name.
 * @return A string containing the file type.
 */
static const char *
get_filetype(const char *filename)
{
	if (strstr(filename, ".gif"))
		return ("image/gif");
	else if (strstr(filename, ".html"))
		return ("text/html");
	else if (strstr(filename, ".jpg"))
		return ("image/jpeg");
	else if (strstr(filename, ".pdf"))
		return ("application/pdf");
	else if (strstr(filename, ".png"))
		return ("image/png");
	else if (strstr(filename, ".css"))
		return ("text/css");
	else
		return ("text/plain");
}

/*
 * Returns an error message to the client.
 *
 * @param fd The descriptor for the client connection.
 * @param cause A string with the HTTP error response code.
 * @param errnum An HTTP status code.
 * @param shortmsg A short message describing the error.
 * @param longmsg A long message describing the error.
 */
static void
client_error(int fd, const char *cause, const char *errnum,
    const char *shortmsg, const char *longmsg)
{
	char buf[MAXLINE];
	int count;

	// Print the HTTP response headers.
	count = snprintf(buf, MAXLINE,
	    "HTTP/1.0 %s %s\r\n"
	    "Content-type: text/html\r\n\r\n",
	    errnum, shortmsg);
	assert(count < MAXLINE);
	if (rio_writen(fd, buf, count) != count) {
		perror("rio_writen");
		return;
	}

	// Print the HTTP response body.
	count = snprintf(buf, MAXLINE,
	    "<html><title>Tiny Error</title>"
	    "<body bgcolor=\"ffffff\">\r\n"
	    "%s: %s\r\n"
	    "<p>%s: %s\r\n"
	    "<hr><em>The Tiny Web server</em>\r\n",
	    errnum, shortmsg, longmsg, cause);
	assert(count < MAXLINE);
	if (rio_writen(fd, buf, count) != count) {
		perror("rio_writen");
		return;
	}
}
