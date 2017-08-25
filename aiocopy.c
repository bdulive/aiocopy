#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/syscall.h>
#include <linux/aio_abi.h>

#define	BUF_SIZE	4096
#define MIN_EVENTS_NR	2
#define MAX_EVENTS_NR	64

inline int io_setup(unsigned nr, aio_context_t *ctxp) {
	return syscall(__NR_io_setup, nr, ctxp);
}

inline int io_destroy(aio_context_t ctx) {
	return syscall(__NR_io_destroy, ctx);
}

inline int io_submit(aio_context_t ctx, long nr, struct iocb **iocbpp) {
	return syscall(__NR_io_submit, ctx, nr, iocbpp);
}

inline int io_getevents(aio_context_t ctx, long min_nr, long max_nr,
		struct io_event *events, struct timespec *timeout) {
	return syscall(__NR_io_getevents, ctx, min_nr, max_nr, events, timeout);
}


int setup_iocb(struct iocb *iocbp, int fd, void *buf, size_t len, off_t pos, unsigned int opcode)
{
	memset(iocbp, 0, sizeof(*iocbp));

	iocbp->aio_fildes = fd;
	iocbp->aio_lio_opcode = opcode;

	iocbp->aio_buf = (uint64_t)buf;
	iocbp->aio_nbytes = len;

	iocbp->aio_offset = pos; /* absolute offset in file */

	return 0;
}


int main(int argc, char *argv[])
{
	int fdin, fdout;
	aio_context_t ctx;
	int ret;
	size_t i, pos, len;

	struct iocb *iocbpp[MAX_EVENTS_NR];
	struct io_event events[MAX_EVENTS_NR];
	char *bufs[MAX_EVENTS_NR] = {0};

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

	/* Alloc and set up IO control block for read */
	for (i = pos = 0, len = BUF_SIZE; i < MAX_EVENTS_NR; i++, pos += len) {
		iocbpp[i] = malloc(sizeof(struct iocb));
		if (iocbpp[i] == NULL) {
			perror("malloc iocb");  
			goto out;	
		}

		bufs[i] = malloc(len);
		if (bufs[i] == NULL) {
		  perror("malloc buf");
		  goto out;
		}

		setup_iocb(iocbpp[i], fdin, bufs[i], len, pos, IOCB_CMD_PREAD);
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

	/* Get events of read. To simplify, wait infinitely for max events */
	ret = io_getevents(ctx, MAX_EVENTS_NR, MAX_EVENTS_NR, events, NULL);
	printf("read events: %d\n", ret);

	/* Got events only if ret > 0 */
	if(ret <= 0) {
		perror("io_getevents for read");
		goto out;
	}

	/* Set up IO control block for write, and attach buffer with read data */
	for (i = pos = 0; i < MAX_EVENTS_NR; i++, pos += len)
		setup_iocb(iocbpp[i], fdout, bufs[i], len, pos, IOCB_CMD_PWRITE);

	/* Submit IOs to write */
	ret = io_submit(ctx, MAX_EVENTS_NR, iocbpp);

	if (ret != MAX_EVENTS_NR) {
		if (ret < 0) 
			perror("io_submit for write");
		else
			printf("io_submit for write failed\n");

		goto out;
	}

	/* Get events of write. To simplify, wait infinitely for max events */
	ret = io_getevents(ctx, MAX_EVENTS_NR, MAX_EVENTS_NR, events, NULL);
	printf("write events: %d\n", ret);

	/* Got events only if ret > 0 */
	if(ret <= 0) {
		perror("io_getevents for write");
		goto out;
	}

out:
	/* Release all resources */
	for (i = 0; i < MAX_EVENTS_NR; i++) {
		if (!iocbpp[i])
		  break;
		free(iocbpp[i]);
	}

	for (i = 0; i < MAX_EVENTS_NR; i++) {
		if (!bufs[i])
		  break;
		free(bufs[i]);
	}

	ret = io_destroy(ctx);
	if (ret < 0)
		perror("io_destroy error");

	return ret;
}
