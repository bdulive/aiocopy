#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/syscall.h>
#include <linux/aio_abi.h>

#define	BUF_SIZE	4096
#define MIN_EVENTS_NR	8
#define MAX_EVENTS_NR	64

/* sys_io_setup:
 *  Create an aio_context capable of receiving at least nr_events.
 *  ctxp must not point to an aio_context that already exists, and
 *  must be initialized to 0 prior to the call.  On successful
 *  creation of the aio_context, *ctxp is filled in with the resulting 
 *  handle.  May fail with -EINVAL if *ctxp is not initialized,
 *  if the specified nr_events exceeds internal limits.  May fail 
 *  with -EAGAIN if the specified nr_events exceeds the user's limit 
 *  of available events.  May fail with -ENOMEM if insufficient kernel
 *  resources are available.  May fail with -EFAULT if an invalid
 *  pointer is passed for ctxp.  Will fail with -ENOSYS if not
 *  implemented.
 */
inline int io_setup(unsigned nr, aio_context_t *ctxp) {
	return syscall(__NR_io_setup, nr, ctxp);
}

/* sys_io_destroy:
 *  Destroy the aio_context specified.  May cancel any outstanding 
 *  AIOs and block on completion.  Will fail with -ENOSYS if not
 *  implemented.  May fail with -EINVAL if the context pointed to
 *  is invalid.
 */
inline int io_destroy(aio_context_t ctx) {
	return syscall(__NR_io_destroy, ctx);
}

/* sys_io_submit:
 *  Queue the nr iocbs pointed to by iocbpp for processing.  Returns
 *  the number of iocbs queued.  May return -EINVAL if the aio_context
 *  specified by ctx_id is invalid, if nr is < 0, if the iocb at
 *  *iocbpp[0] is not properly initialized, if the operation specified
 *  is invalid for the file descriptor in the iocb.  May fail with
 *  -EFAULT if any of the data structures point to invalid data.  May
 *  fail with -EBADF if the file descriptor specified in the first
 *  iocb is invalid.  May fail with -EAGAIN if insufficient resources
 *  are available to queue any iocbs.  Will return 0 if nr is 0.  Will
 *  fail with -ENOSYS if not implemented.
 */
inline int io_submit(aio_context_t ctx, long nr, struct iocb **iocbpp) {
	return syscall(__NR_io_submit, ctx, nr, iocbpp);
}

/* io_getevents:
 *  Attempts to read at least min_nr events and up to nr events from
 *  the completion queue for the aio_context specified by ctx_id. If
 *  it succeeds, the number of read events is returned. May fail with
 *  -EINVAL if ctx_id is invalid, if min_nr is out of range, if nr is
 *  out of range, if timeout is out of range.  May fail with -EFAULT
 *  if any of the memory specified is invalid.  May return 0 or
 *  < min_nr if the timeout specified by timeout has elapsed
 *  before sufficient events are available, where timeout == NULL
 *  specifies an infinite timeout. Note that the timeout pointed to by
 *  timeout is relative.  Will fail with -ENOSYS if not implemented.
 */
inline int io_getevents(aio_context_t ctx, long min_nr, long max_nr,
		struct io_event *events, struct timespec *timeout) {
	return syscall(__NR_io_getevents, ctx, min_nr, max_nr, events, timeout);
}


int setup_iocb(struct iocb *iocbp, int fd, void *buf, size_t count, off_t pos, unsigned int opcode)
{
	memset(iocbp, 0, sizeof(*iocbp));

	iocbp->aio_fildes = fd;
	iocbp->aio_lio_opcode = opcode;

	iocbp->aio_buf = (uint64_t)buf;
	iocbp->aio_nbytes = count;

	iocbp->aio_offset = pos; /* absolute offset in file */

	return 0;
}


int main(int argc, char *argv[])
{
	int fdin, fdout;
	aio_context_t ctx;
	int ret;
	size_t count, pos;

	struct iocb iocb;
	struct iocb *iocbpp[MAX_EVENTS_NR];
	struct io_event events[MAX_EVENTS_NR];
	char buf[MAX_EVENTS_NR][BUF_SIZE] = {0};

	if (argc != 3) {
		printf("usage: %s <fromfile> <tofile>\n", argv[0]);
		return -1;
	}

	if ((fdin = open(argv[1], O_RDONLY)) < 0) {
		perror(argv[1]);
		return -1;
	}

	if ((fdout = open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 
						S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
		perror(argv[2]);
		return -1;
	}

	ctx = 0;

	/* Create an aio_context capable of receiving at least nr_events. */
	ret = io_setup(MAX_EVENTS_NR, &ctx);
	if (ret < 0) {
		perror("io_setup");
		return -1;
	}

	count = BUF_SIZE;

	int i;
	/* Alloc and set up IO control block for read */
	for (i = pos = 0; i < MAX_EVENTS_NR; i++, pos += count) {
		iocbpp[i] = malloc(sizeof(struct iocb));
		if (iocbpp[i] == NULL) {
			perror("malloc");  
			goto out;	
		}
		setup_iocb(iocbpp[i], fdin, buf[i], count, pos, IOCB_CMD_PREAD);
	}


	/* Submit IOs to read */
	ret = io_submit(ctx, MAX_EVENTS_NR, iocbpp);

	if (ret != MAX_EVENTS_NR) {
		if (ret < 0) 
			perror("io_submit for read");
		else
			printf("io_submit for read failed\n");

		goto out;
	}

	int got = 0;
	/* Get events of read */
	do {	
		printf("Doing io_getevents ...\n");

		ret = io_getevents(ctx, MIN_EVENTS_NR, MAX_EVENTS_NR, events, NULL);

		printf("read events: %d\n", ret);

		if(ret < 0) {
			perror("io_getevents for read");
			goto out;
		}

		for (i = 0; i < ret ;i++)
			if (write(fdout, buf[i], count) != count) {
				perror("write");
				ret = -1;
				goto out;
			}
	} while ((got += ret) < MAX_EVENTS_NR);

out:
	/* Release all buffer */
	for (i = 0; i < MAX_EVENTS_NR; i++) 
		free(iocbpp[i]);

	ret = io_destroy(ctx);
	if (ret < 0) {
		perror("io_destroy error");
		return -1;
	}

	return ret;
}
