#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#ifndef ETIME
#define ETIME ETIMEDOUT
#endif

struct timeval add2currenttime(double seconds);
int copyfile(int fromfd, int tofd);
int r_close(int fildes);
int r_dup2(int fildes, int fildes2);
int r_open2(const char *path, int oflag);
int r_open3(const char *path, int oflag, mode_t mode);
ssize_t r_read(int fd, void *buf, size_t size);
pid_t r_wait(int *stat_loc);
pid_t r_waitpid(pid_t pid, int *stat_loc, int options);
ssize_t r_write(int fd, void *buf, size_t size);
ssize_t readblock(int fd, void *buf, size_t size);
int readline(int fd, char *buf, int nbytes);
ssize_t readtimed(int fd, void *buf, size_t nbyte, double seconds);
int readwrite(int fromfd, int tofd);
int readwriteblock(int fromfd, int tofd, char *buf, int size);
int waitfdtimed(int fd, struct timeval end);
