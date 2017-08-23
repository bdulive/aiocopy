#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <linux/aio_abi.h>

#define	BUFFSIZE	4096

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


static inline int read_request(int fd, void *buf, size_t count, aio_context_t *pctx)
{
	return io_request(fd, buf, count, pctx, 0);
}

static inline int write_request(int fd, void *buf, size_t count, aio_context_t *pctx)
{
	return io_request(fd, buf, count, pctx, 1);
}


int io_request(int fd, void *buf, size_t count, aio_context_t *pctx, int write)
{
	struct iocb cb;
	struct iocb *cbs[1];
	struct io_event events[1];
	int ret;

	/* setup I/O control block */
	memset(&cb, 0, sizeof(cb));
	cb.aio_fildes = fd;
	cb.aio_lio_opcode = (write ? IOCB_CMD_PWRITE : IOCB_CMD_PREAD);

	/* command-specific options */
	cb.aio_buf = (uint64_t)buf;
	cb.aio_offset = 0;
	cb.aio_nbytes = count;

	cbs[0] = &cb;

	ret = io_submit(*pctx, 1, cbs);
	if (ret != 1) {
		if (ret < 0) 
			perror("io_submit");
		else 
			fprintf(stderr, "io_submit failed\n");

		return -1;
	}

	/* get reply */
	ret = io_getevents(*pctx, 1, 1, events, NULL);
	printf("events: %d\n", ret);

	return 0;
}

int
main(int argc, char *argv[])
{
	int		fdin, fdout;

	aio_context_t ctx;
	struct iocb cb;
	struct iocb *cbs[1];
	char buf[BUFFSIZE] = {0};
	struct io_event events[1];
	int ret;

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

	ret = io_setup(128, &ctx);
	if (ret < 0) {
		perror("io_setup");
		return -1;
	}

	if (read_request(fdin, buf, BUFFSIZE, &ctx) < 0) 
		return -1;

	if (write_request(fdout, buf, BUFFSIZE, &ctx) < 0) 
		return -1;

	return 0;
}
