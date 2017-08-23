#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <linux/aio_abi.h>

#define	BUF_SIZE	4096
#define MIN_IO_NR	1
#define MAX_IO_NR	128

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


static int io_request(int fd, void *buf, size_t count, off_t pos, aio_context_t *pctx, int is_write)
{
	struct iocb cb;
	struct iocb *cbs[MIN_IO_NR];
	struct io_event events[MIN_IO_NR];
	int ret;

	/* setup I/O control block */
	memset(&cb, 0, sizeof(cb));
	cb.aio_fildes = fd;
	cb.aio_lio_opcode = (is_write ? IOCB_CMD_PWRITE : IOCB_CMD_PREAD);

	cb.aio_buf = (uint64_t)buf;
	cb.aio_nbytes = count;

	cb.aio_offset = pos; /* absolute offset in file */

	cbs[0] = &cb;

	ret = io_submit(*pctx, 1, cbs);
	if (ret != 1) {
		if (ret < 0) 
			perror("io_submit");

		printf("io_submit(%d) failed\n", is_write);

		return -1;
	}

	/* get reply */
	ret = io_getevents(*pctx, 1, 1, events, NULL);
	printf("events(%d): %d\n", is_write, ret);

	return ret;
}

static inline int read_request(int fd, void *buf, size_t count, off_t pos, aio_context_t *pctx)
{
	return io_request(fd, buf, count, pos, pctx, 0);
}

static inline int write_request(int fd, void *buf, size_t count, off_t pos, aio_context_t *pctx)
{
	return io_request(fd, buf, count, pos, pctx, 1);
}


int main(int argc, char *argv[])
{
	int fdin, fdout;
	aio_context_t ctx;
	char buf[BUF_SIZE] = {0};
	int ret;
	int count, pos;

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

	ret = io_setup(MAX_IO_NR, &ctx);
	if (ret < 0) {
		perror("io_setup");
		return -1;
	}

	count = sizeof(buf);
	pos = 0;

	if (read_request(fdin, buf, count, pos, &ctx) <= 0)
			return -1;

	if (write_request(fdout, buf, count, pos, &ctx) <= 0) 
			return -1;

	return 0;
}
