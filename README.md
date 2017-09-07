# aiocopy

Copy with linux native AIO system call: io_setup, io_destroy, io_submit, io_getevents, ...

Just be used to trigger linux kernel file operation aio_read/aio_write or read_iter/write_iter,
not acting as a correct copy program (no handle of real file size).

