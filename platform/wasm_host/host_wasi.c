// Minimal POSIX-backed WASI preview1 host for the macOS wasm sandbox build.
//
// Implements the subset of wasi_snapshot_preview1 that wasi-libc needs for the
// game's stdio, file, directory and clock use. Guest fds index a small table of
// host fds; fd 3 is a preopen of "/" so absolute host paths work unchanged.
// This is not a security sandbox: the guest is trusted code.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "host_mem.h"
#include "host_wasi.h"

extern char **environ;

#define WASI_ERRNO_SUCCESS     0
#define WASI_ERRNO_2BIG        1
#define WASI_ERRNO_ACCES       2
#define WASI_ERRNO_AGAIN       6
#define WASI_ERRNO_BADF        8
#define WASI_ERRNO_BUSY        10
#define WASI_ERRNO_EXIST       20
#define WASI_ERRNO_FBIG        22
#define WASI_ERRNO_INTR        27
#define WASI_ERRNO_INVAL       28
#define WASI_ERRNO_IO          29
#define WASI_ERRNO_ISDIR       31
#define WASI_ERRNO_LOOP        32
#define WASI_ERRNO_MFILE       33
#define WASI_ERRNO_NAMETOOLONG 37
#define WASI_ERRNO_NFILE       41
#define WASI_ERRNO_NOENT       44
#define WASI_ERRNO_NOMEM       48
#define WASI_ERRNO_NOSPC       51
#define WASI_ERRNO_NOSYS       52
#define WASI_ERRNO_NOTDIR      54
#define WASI_ERRNO_NOTEMPTY    55
#define WASI_ERRNO_NOTSUP      58
#define WASI_ERRNO_PERM        63
#define WASI_ERRNO_ROFS        69
#define WASI_ERRNO_SPIPE       70
#define WASI_ERRNO_XDEV        75

#define WASI_FILETYPE_UNKNOWN          0
#define WASI_FILETYPE_BLOCK_DEVICE     1
#define WASI_FILETYPE_CHARACTER_DEVICE 2
#define WASI_FILETYPE_DIRECTORY        3
#define WASI_FILETYPE_REGULAR_FILE     4
#define WASI_FILETYPE_SOCKET_STREAM    6
#define WASI_FILETYPE_SYMBOLIC_LINK    7

#define WASI_OFLAGS_CREAT     (1u << 0)
#define WASI_OFLAGS_DIRECTORY (1u << 1)
#define WASI_OFLAGS_EXCL      (1u << 2)
#define WASI_OFLAGS_TRUNC     (1u << 3)

#define WASI_FDFLAGS_APPEND (1u << 0)

#define WASI_RIGHTS_FD_READ  (1ull << 1)
#define WASI_RIGHTS_FD_SEEK  (1ull << 2)
#define WASI_RIGHTS_FD_TELL  (1ull << 5)
#define WASI_RIGHTS_FD_WRITE (1ull << 6)

#define WASI_LOOKUPFLAGS_SYMLINK_FOLLOW (1u << 0)

#define WASI_EVENTTYPE_CLOCK 0

#define HOST_WASI_FD_MAX     128
#define HOST_WASI_PREOPEN_FD 3

struct HostWasiFd
{
	int hostFd;   // -1 when the slot is free
	DIR *dir;     // lazily opened for fd_readdir
	int isPreopen;
};

static struct HostWasiFd s_fds[HOST_WASI_FD_MAX];
static int s_argc;
static char **s_argv;

void HostWasi_Init(int argc, char **argv)
{
	s_argc = argc;
	s_argv = argv;

	for (int i = 0; i < HOST_WASI_FD_MAX; i++)
	{
		s_fds[i].hostFd = -1;
		s_fds[i].dir = NULL;
		s_fds[i].isPreopen = 0;
	}

	s_fds[0].hostFd = STDIN_FILENO;
	s_fds[1].hostFd = STDOUT_FILENO;
	s_fds[2].hostFd = STDERR_FILENO;

	s_fds[HOST_WASI_PREOPEN_FD].hostFd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	s_fds[HOST_WASI_PREOPEN_FD].isPreopen = 1;
	if (s_fds[HOST_WASI_PREOPEN_FD].hostFd < 0)
	{
		HostMem_Fatal("could not preopen /: %s", strerror(errno));
	}
}

static u32 HostWasi_Errno(int err)
{
	switch (err)
	{
	case 0: return WASI_ERRNO_SUCCESS;
	case E2BIG: return WASI_ERRNO_2BIG;
	case EACCES: return WASI_ERRNO_ACCES;
	case EAGAIN: return WASI_ERRNO_AGAIN;
	case EBADF: return WASI_ERRNO_BADF;
	case EBUSY: return WASI_ERRNO_BUSY;
	case EEXIST: return WASI_ERRNO_EXIST;
	case EFBIG: return WASI_ERRNO_FBIG;
	case EINTR: return WASI_ERRNO_INTR;
	case EINVAL: return WASI_ERRNO_INVAL;
	case EIO: return WASI_ERRNO_IO;
	case EISDIR: return WASI_ERRNO_ISDIR;
	case ELOOP: return WASI_ERRNO_LOOP;
	case EMFILE: return WASI_ERRNO_MFILE;
	case ENAMETOOLONG: return WASI_ERRNO_NAMETOOLONG;
	case ENFILE: return WASI_ERRNO_NFILE;
	case ENOENT: return WASI_ERRNO_NOENT;
	case ENOMEM: return WASI_ERRNO_NOMEM;
	case ENOSPC: return WASI_ERRNO_NOSPC;
	case ENOSYS: return WASI_ERRNO_NOSYS;
	case ENOTDIR: return WASI_ERRNO_NOTDIR;
	case ENOTEMPTY: return WASI_ERRNO_NOTEMPTY;
	case ENOTSUP: return WASI_ERRNO_NOTSUP;
	case EPERM: return WASI_ERRNO_PERM;
	case EROFS: return WASI_ERRNO_ROFS;
	case ESPIPE: return WASI_ERRNO_SPIPE;
	case EXDEV: return WASI_ERRNO_XDEV;
	default: return WASI_ERRNO_IO;
	}
}

static struct HostWasiFd *HostWasi_GetFd(u32 fd)
{
	if ((fd >= HOST_WASI_FD_MAX) || (s_fds[fd].hostFd < 0))
	{
		return NULL;
	}

	return &s_fds[fd];
}

// Copies a guest path into a NUL-terminated host buffer. Returns 0 on success.
static u32 HostWasi_CopyPath(char *dst, size_t dstSize, u32 path, u32 pathLen)
{
	if (pathLen >= dstSize)
	{
		return WASI_ERRNO_NAMETOOLONG;
	}

	memcpy(dst, HOST_G2H_SIZED(path, pathLen), pathLen);
	dst[pathLen] = '\0';

	// NOTE: openat() and friends treat an empty path as an error; WASI uses it for the directory itself.
	if (dst[0] == '\0')
	{
		dst[0] = '.';
		dst[1] = '\0';
	}

	return WASI_ERRNO_SUCCESS;
}

static uint8_t HostWasi_FileType(mode_t mode)
{
	if (S_ISREG(mode))
		return WASI_FILETYPE_REGULAR_FILE;
	if (S_ISDIR(mode))
		return WASI_FILETYPE_DIRECTORY;
	if (S_ISCHR(mode))
		return WASI_FILETYPE_CHARACTER_DEVICE;
	if (S_ISBLK(mode))
		return WASI_FILETYPE_BLOCK_DEVICE;
	if (S_ISLNK(mode))
		return WASI_FILETYPE_SYMBOLIC_LINK;
	if (S_ISSOCK(mode))
		return WASI_FILETYPE_SOCKET_STREAM;
	return WASI_FILETYPE_UNKNOWN;
}

static uint64_t HostWasi_TimespecNs(struct timespec ts)
{
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void HostWasi_WriteFilestat(u32 out, const struct stat *st)
{
	uint8_t *dst = HOST_G2H_SIZED(out, 64);

	memset(dst, 0, 64);
	uint64_t dev = (uint64_t)st->st_dev;
	uint64_t ino = (uint64_t)st->st_ino;
	uint64_t nlink = (uint64_t)st->st_nlink;
	uint64_t size = (uint64_t)st->st_size;
	uint64_t atim = HostWasi_TimespecNs(st->st_atimespec);
	uint64_t mtim = HostWasi_TimespecNs(st->st_mtimespec);
	uint64_t ctim = HostWasi_TimespecNs(st->st_ctimespec);

	memcpy(dst + 0, &dev, 8);
	memcpy(dst + 8, &ino, 8);
	dst[16] = HostWasi_FileType(st->st_mode);
	memcpy(dst + 24, &nlink, 8);
	memcpy(dst + 32, &size, 8);
	memcpy(dst + 40, &atim, 8);
	memcpy(dst + 48, &mtim, 8);
	memcpy(dst + 56, &ctim, 8);
}

static void HostWasi_StoreU32(u32 out, uint32_t value)
{
	memcpy(HOST_G2H_SIZED(out, 4), &value, 4);
}

static void HostWasi_StoreU64(u32 out, uint64_t value)
{
	memcpy(HOST_G2H_SIZED(out, 8), &value, 8);
}

// ---- args / environ --------------------------------------------------------

static u32 HostWasi_StringsSizes(char **strings, int count, u32 outCount, u32 outBufSize)
{
	uint32_t bufSize = 0;

	for (int i = 0; i < count; i++)
	{
		bufSize += (uint32_t)strlen(strings[i]) + 1;
	}

	HostWasi_StoreU32(outCount, (uint32_t)count);
	HostWasi_StoreU32(outBufSize, bufSize);
	return WASI_ERRNO_SUCCESS;
}

static u32 HostWasi_StringsGet(char **strings, int count, u32 ptrs, u32 buf)
{
	uint32_t cursor = buf;

	for (int i = 0; i < count; i++)
	{
		size_t len = strlen(strings[i]) + 1;
		HostWasi_StoreU32(ptrs + (u32)i * 4, cursor);
		memcpy(HOST_G2H_SIZED(cursor, len), strings[i], len);
		cursor += (uint32_t)len;
	}

	return WASI_ERRNO_SUCCESS;
}

static int HostWasi_EnvironCount(void)
{
	int count = 0;

	while (environ[count] != NULL)
	{
		count++;
	}

	return count;
}

u32 w2c_wasi__snapshot__preview1_args_sizes_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 outCount, u32 outBufSize)
{
	(void)ctx;
	return HostWasi_StringsSizes(s_argv, s_argc, outCount, outBufSize);
}

u32 w2c_wasi__snapshot__preview1_args_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 ptrs, u32 buf)
{
	(void)ctx;
	return HostWasi_StringsGet(s_argv, s_argc, ptrs, buf);
}

u32 w2c_wasi__snapshot__preview1_environ_sizes_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 outCount, u32 outBufSize)
{
	(void)ctx;
	return HostWasi_StringsSizes(environ, HostWasi_EnvironCount(), outCount, outBufSize);
}

u32 w2c_wasi__snapshot__preview1_environ_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 ptrs, u32 buf)
{
	(void)ctx;
	return HostWasi_StringsGet(environ, HostWasi_EnvironCount(), ptrs, buf);
}

// ---- process / clocks ------------------------------------------------------

static void (*s_exitHandler)(int code);

void HostWasi_SetExitHandler(void (*handler)(int code))
{
	s_exitHandler = handler;
}

void w2c_wasi__snapshot__preview1_proc_exit(struct w2c_wasi__snapshot__preview1 *ctx, u32 code)
{
	(void)ctx;
	fflush(stdout);
	fflush(stderr);
	if ((code != 0) && (s_exitHandler != NULL))
	{
		s_exitHandler((int)code);
	}
	exit((int)code);
}

u32 w2c_wasi__snapshot__preview1_clock_time_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 clockId, u64 precision, u32 out)
{
	struct timespec ts;
	clockid_t hostClock;

	(void)ctx;
	(void)precision;

	switch (clockId)
	{
	case 0: hostClock = CLOCK_REALTIME; break;
	case 1: hostClock = CLOCK_MONOTONIC; break;
	case 2: hostClock = CLOCK_PROCESS_CPUTIME_ID; break;
	case 3: hostClock = CLOCK_THREAD_CPUTIME_ID; break;
	default: return WASI_ERRNO_INVAL;
	}

	if (clock_gettime(hostClock, &ts) != 0)
	{
		return HostWasi_Errno(errno);
	}

	HostWasi_StoreU64(out, HostWasi_TimespecNs(ts));
	return WASI_ERRNO_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_clock_res_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 clockId, u32 out)
{
	(void)ctx;
	(void)clockId;
	HostWasi_StoreU64(out, 1);
	return WASI_ERRNO_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_random_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 buf, u32 len)
{
	(void)ctx;
	arc4random_buf(HOST_G2H_SIZED(buf, len), len);
	return WASI_ERRNO_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_sched_yield(struct w2c_wasi__snapshot__preview1 *ctx)
{
	(void)ctx;
	sched_yield();
	return WASI_ERRNO_SUCCESS;
}

// NOTE: Only relative clock subscriptions (sleep/nanosleep) are supported.
u32 w2c_wasi__snapshot__preview1_poll_oneoff(struct w2c_wasi__snapshot__preview1 *ctx, u32 in, u32 out, u32 count, u32 outCount)
{
	(void)ctx;
	uint32_t events = 0;

	for (u32 i = 0; i < count; i++)
	{
		const uint8_t *sub = HOST_G2H_SIZED(in + i * 48, 48);
		uint8_t *evt = HOST_G2H_SIZED(out + events * 32, 32);
		uint64_t timeout;

		memset(evt, 0, 32);
		memcpy(evt, sub, 8); // userdata
		evt[10] = sub[8];

		if (sub[8] != WASI_EVENTTYPE_CLOCK)
		{
			uint16_t err = WASI_ERRNO_NOTSUP;
			memcpy(evt + 8, &err, 2);
			events++;
			continue;
		}

		memcpy(&timeout, sub + 24, 8);
		struct timespec ts = {(time_t)(timeout / 1000000000ull), (long)(timeout % 1000000000ull)};
		while ((nanosleep(&ts, &ts) != 0) && (errno == EINTR))
		{
		}
		events++;
	}

	HostWasi_StoreU32(outCount, events);
	return WASI_ERRNO_SUCCESS;
}

// ---- fds -------------------------------------------------------------------

u32 w2c_wasi__snapshot__preview1_fd_prestat_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u32 out)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);

	if ((entry == NULL) || !entry->isPreopen)
	{
		return WASI_ERRNO_BADF;
	}

	uint8_t *dst = HOST_G2H_SIZED(out, 8);
	memset(dst, 0, 8);
	uint32_t nameLen = 1; // "/"
	memcpy(dst + 4, &nameLen, 4);
	return WASI_ERRNO_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_fd_prestat_dir_name(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u32 path, u32 pathLen)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);

	if ((entry == NULL) || !entry->isPreopen)
	{
		return WASI_ERRNO_BADF;
	}

	if (pathLen < 1)
	{
		return WASI_ERRNO_NAMETOOLONG;
	}

	*(char *)HOST_G2H_SIZED(path, 1) = '/';
	return WASI_ERRNO_SUCCESS;
}

static u32 HostWasi_ReadWrite(u32 fd, u32 iovs, u32 iovsLen, u32 outBytes, int isWrite)
{
	struct HostWasiFd *entry = HostWasi_GetFd(fd);
	struct iovec hostIovs[64];

	if (entry == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	if (iovsLen > 64)
	{
		iovsLen = 64;
	}

	for (u32 i = 0; i < iovsLen; i++)
	{
		const uint8_t *iov = HOST_G2H_SIZED(iovs + i * 8, 8);
		uint32_t buf;
		uint32_t len;

		memcpy(&buf, iov, 4);
		memcpy(&len, iov + 4, 4);
		hostIovs[i].iov_base = (len != 0) ? HOST_G2H_SIZED(buf, len) : NULL;
		hostIovs[i].iov_len = len;
	}

	ssize_t result = isWrite ? writev(entry->hostFd, hostIovs, (int)iovsLen) : readv(entry->hostFd, hostIovs, (int)iovsLen);
	if (result < 0)
	{
		return HostWasi_Errno(errno);
	}

	HostWasi_StoreU32(outBytes, (uint32_t)result);
	return WASI_ERRNO_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_fd_read(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u32 iovs, u32 iovsLen, u32 outBytes)
{
	(void)ctx;
	return HostWasi_ReadWrite(fd, iovs, iovsLen, outBytes, 0);
}

u32 w2c_wasi__snapshot__preview1_fd_write(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u32 iovs, u32 iovsLen, u32 outBytes)
{
	(void)ctx;
	return HostWasi_ReadWrite(fd, iovs, iovsLen, outBytes, 1);
}

u32 w2c_wasi__snapshot__preview1_fd_seek(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u64 offset, u32 whence, u32 outOffset)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);
	int hostWhence;

	if (entry == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	switch (whence)
	{
	case 0: hostWhence = SEEK_SET; break;
	case 1: hostWhence = SEEK_CUR; break;
	case 2: hostWhence = SEEK_END; break;
	default: return WASI_ERRNO_INVAL;
	}

	off_t result = lseek(entry->hostFd, (off_t)(int64_t)offset, hostWhence);
	if (result < 0)
	{
		return HostWasi_Errno(errno);
	}

	HostWasi_StoreU64(outOffset, (uint64_t)result);
	return WASI_ERRNO_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_fd_tell(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u32 outOffset)
{
	return w2c_wasi__snapshot__preview1_fd_seek(ctx, fd, 0, 1, outOffset);
}

u32 w2c_wasi__snapshot__preview1_fd_close(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);

	if ((entry == NULL) || entry->isPreopen)
	{
		return WASI_ERRNO_BADF;
	}

	int result;
	if (entry->dir != NULL)
	{
		closedir(entry->dir);
		entry->dir = NULL;
	}

	result = (fd > 2) ? close(entry->hostFd) : 0;
	entry->hostFd = -1;
	return (result == 0) ? WASI_ERRNO_SUCCESS : HostWasi_Errno(errno);
}

u32 w2c_wasi__snapshot__preview1_fd_sync(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);

	if (entry == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	return (fsync(entry->hostFd) == 0) ? WASI_ERRNO_SUCCESS : HostWasi_Errno(errno);
}

u32 w2c_wasi__snapshot__preview1_fd_fdstat_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u32 out)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);
	struct stat st;

	if (entry == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	if (fstat(entry->hostFd, &st) != 0)
	{
		return HostWasi_Errno(errno);
	}

	uint8_t *dst = HOST_G2H_SIZED(out, 24);
	memset(dst, 0, 24);
	dst[0] = HostWasi_FileType(st.st_mode);

	int fl = fcntl(entry->hostFd, F_GETFL);
	uint16_t flags = ((fl >= 0) && (fl & O_APPEND)) ? WASI_FDFLAGS_APPEND : 0;
	memcpy(dst + 2, &flags, 2);

	// NOTE: wasi-libc's isatty() requires a character device without seek/tell
	// rights; that is what makes stdout line-buffered on a terminal.
	uint64_t rights = ~0ull;
	if (isatty(entry->hostFd))
	{
		rights &= ~(WASI_RIGHTS_FD_SEEK | WASI_RIGHTS_FD_TELL);
	}
	memcpy(dst + 8, &rights, 8);
	memcpy(dst + 16, &rights, 8);
	return WASI_ERRNO_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_fd_fdstat_set_flags(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u32 flags)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);

	if (entry == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	int fl = fcntl(entry->hostFd, F_GETFL);
	if (fl < 0)
	{
		return HostWasi_Errno(errno);
	}

	fl = (flags & WASI_FDFLAGS_APPEND) ? (fl | O_APPEND) : (fl & ~O_APPEND);
	return (fcntl(entry->hostFd, F_SETFL, fl) == 0) ? WASI_ERRNO_SUCCESS : HostWasi_Errno(errno);
}

u32 w2c_wasi__snapshot__preview1_fd_filestat_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u32 out)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);
	struct stat st;

	if (entry == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	if (fstat(entry->hostFd, &st) != 0)
	{
		return HostWasi_Errno(errno);
	}

	HostWasi_WriteFilestat(out, &st);
	return WASI_ERRNO_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_fd_filestat_set_size(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u64 size)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);

	if (entry == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	return (ftruncate(entry->hostFd, (off_t)size) == 0) ? WASI_ERRNO_SUCCESS : HostWasi_Errno(errno);
}

// NOTE: The cookie is the index of the next entry. Each call rewinds and skips,
// which is fine for the small directories the game scans.
u32 w2c_wasi__snapshot__preview1_fd_readdir(struct w2c_wasi__snapshot__preview1 *ctx, u32 fd, u32 buf, u32 bufLen, u64 cookie, u32 outUsed)
{
	(void)ctx;
	struct HostWasiFd *entry = HostWasi_GetFd(fd);

	if (entry == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	if (entry->dir == NULL)
	{
		int dupFd = dup(entry->hostFd);
		if (dupFd < 0)
		{
			return HostWasi_Errno(errno);
		}

		entry->dir = fdopendir(dupFd);
		if (entry->dir == NULL)
		{
			close(dupFd);
			return HostWasi_Errno(errno);
		}
	}

	rewinddir(entry->dir);

	uint8_t *dst = HOST_G2H_SIZED(buf, bufLen);
	uint32_t used = 0;
	uint64_t index = 0;
	struct dirent *de;

	while ((de = readdir(entry->dir)) != NULL)
	{
		if (index++ < cookie)
		{
			continue;
		}

		uint8_t header[24];
		uint64_t next = index;
		uint64_t ino = (uint64_t)de->d_ino;
		uint32_t nameLen = (uint32_t)strlen(de->d_name);
		uint8_t type = WASI_FILETYPE_UNKNOWN;

		switch (de->d_type)
		{
		case DT_REG: type = WASI_FILETYPE_REGULAR_FILE; break;
		case DT_DIR: type = WASI_FILETYPE_DIRECTORY; break;
		case DT_LNK: type = WASI_FILETYPE_SYMBOLIC_LINK; break;
		case DT_CHR: type = WASI_FILETYPE_CHARACTER_DEVICE; break;
		case DT_BLK: type = WASI_FILETYPE_BLOCK_DEVICE; break;
		case DT_SOCK: type = WASI_FILETYPE_SOCKET_STREAM; break;
		default: break;
		}

		memset(header, 0, sizeof(header));
		memcpy(header + 0, &next, 8);
		memcpy(header + 8, &ino, 8);
		memcpy(header + 16, &nameLen, 4);
		header[20] = type;

		// WASI allows the final entry to be truncated; the caller retries with a larger buffer.
		uint32_t chunk = (bufLen - used < sizeof(header)) ? (bufLen - used) : (uint32_t)sizeof(header);
		memcpy(dst + used, header, chunk);
		used += chunk;
		if (used == bufLen)
		{
			break;
		}

		chunk = (bufLen - used < nameLen) ? (bufLen - used) : nameLen;
		memcpy(dst + used, de->d_name, chunk);
		used += chunk;
		if (used == bufLen)
		{
			break;
		}
	}

	HostWasi_StoreU32(outUsed, used);
	return WASI_ERRNO_SUCCESS;
}

// ---- paths -----------------------------------------------------------------

static int HostWasi_AllocFd(int hostFd)
{
	for (int i = HOST_WASI_PREOPEN_FD + 1; i < HOST_WASI_FD_MAX; i++)
	{
		if (s_fds[i].hostFd < 0)
		{
			s_fds[i].hostFd = hostFd;
			s_fds[i].dir = NULL;
			s_fds[i].isPreopen = 0;
			return i;
		}
	}

	return -1;
}

u32 w2c_wasi__snapshot__preview1_path_open(struct w2c_wasi__snapshot__preview1 *ctx, u32 dirFd, u32 lookupFlags, u32 path, u32 pathLen, u32 oflags,
                                           u64 rightsBase, u64 rightsInheriting, u32 fdflags, u32 outFd)
{
	(void)ctx;
	(void)rightsInheriting;
	struct HostWasiFd *dir = HostWasi_GetFd(dirFd);
	char hostPath[4096];
	u32 err;

	if (dir == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	if ((err = HostWasi_CopyPath(hostPath, sizeof(hostPath), path, pathLen)) != WASI_ERRNO_SUCCESS)
	{
		return err;
	}

	int canRead = (rightsBase & WASI_RIGHTS_FD_READ) != 0;
	int canWrite = (rightsBase & WASI_RIGHTS_FD_WRITE) != 0;
	int flags = O_CLOEXEC;

	if (oflags & WASI_OFLAGS_DIRECTORY)
	{
		flags |= O_RDONLY | O_DIRECTORY;
	}
	else if (canRead && canWrite)
	{
		flags |= O_RDWR;
	}
	else if (canWrite)
	{
		flags |= O_WRONLY;
	}
	else
	{
		flags |= O_RDONLY;
	}

	if (oflags & WASI_OFLAGS_CREAT)
		flags |= O_CREAT;
	if (oflags & WASI_OFLAGS_EXCL)
		flags |= O_EXCL;
	if (oflags & WASI_OFLAGS_TRUNC)
		flags |= O_TRUNC;
	if (fdflags & WASI_FDFLAGS_APPEND)
		flags |= O_APPEND;
	if (!(lookupFlags & WASI_LOOKUPFLAGS_SYMLINK_FOLLOW))
		flags |= O_NOFOLLOW;

	int hostFd = openat(dir->hostFd, hostPath, flags, 0666);
	if ((hostFd < 0) && (errno == EISDIR) && !(flags & O_DIRECTORY) && !canWrite)
	{
		// NOTE: wasi-libc opens directories without O_DIRECTORY when probing with open(); allow read-only opens.
		hostFd = openat(dir->hostFd, hostPath, O_RDONLY | O_CLOEXEC);
	}

	if (hostFd < 0)
	{
		return HostWasi_Errno(errno);
	}

	int fd = HostWasi_AllocFd(hostFd);
	if (fd < 0)
	{
		close(hostFd);
		return WASI_ERRNO_MFILE;
	}

	HostWasi_StoreU32(outFd, (uint32_t)fd);
	return WASI_ERRNO_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_path_filestat_get(struct w2c_wasi__snapshot__preview1 *ctx, u32 dirFd, u32 lookupFlags, u32 path, u32 pathLen, u32 out)
{
	(void)ctx;
	struct HostWasiFd *dir = HostWasi_GetFd(dirFd);
	char hostPath[4096];
	struct stat st;
	u32 err;

	if (dir == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	if ((err = HostWasi_CopyPath(hostPath, sizeof(hostPath), path, pathLen)) != WASI_ERRNO_SUCCESS)
	{
		return err;
	}

	int atFlags = (lookupFlags & WASI_LOOKUPFLAGS_SYMLINK_FOLLOW) ? 0 : AT_SYMLINK_NOFOLLOW;
	if (fstatat(dir->hostFd, hostPath, &st, atFlags) != 0)
	{
		return HostWasi_Errno(errno);
	}

	HostWasi_WriteFilestat(out, &st);
	return WASI_ERRNO_SUCCESS;
}

static u32 HostWasi_PathOp(u32 dirFd, u32 path, u32 pathLen, int op)
{
	struct HostWasiFd *dir = HostWasi_GetFd(dirFd);
	char hostPath[4096];
	u32 err;
	int result;

	if (dir == NULL)
	{
		return WASI_ERRNO_BADF;
	}

	if ((err = HostWasi_CopyPath(hostPath, sizeof(hostPath), path, pathLen)) != WASI_ERRNO_SUCCESS)
	{
		return err;
	}

	switch (op)
	{
	case 0: result = mkdirat(dir->hostFd, hostPath, 0777); break;
	case 1: result = unlinkat(dir->hostFd, hostPath, 0); break;
	default: result = unlinkat(dir->hostFd, hostPath, AT_REMOVEDIR); break;
	}

	return (result == 0) ? WASI_ERRNO_SUCCESS : HostWasi_Errno(errno);
}

u32 w2c_wasi__snapshot__preview1_path_create_directory(struct w2c_wasi__snapshot__preview1 *ctx, u32 dirFd, u32 path, u32 pathLen)
{
	(void)ctx;
	return HostWasi_PathOp(dirFd, path, pathLen, 0);
}

u32 w2c_wasi__snapshot__preview1_path_unlink_file(struct w2c_wasi__snapshot__preview1 *ctx, u32 dirFd, u32 path, u32 pathLen)
{
	(void)ctx;
	return HostWasi_PathOp(dirFd, path, pathLen, 1);
}

u32 w2c_wasi__snapshot__preview1_path_remove_directory(struct w2c_wasi__snapshot__preview1 *ctx, u32 dirFd, u32 path, u32 pathLen)
{
	(void)ctx;
	return HostWasi_PathOp(dirFd, path, pathLen, 2);
}

u32 w2c_wasi__snapshot__preview1_path_rename(struct w2c_wasi__snapshot__preview1 *ctx, u32 oldDirFd, u32 oldPath, u32 oldPathLen, u32 newDirFd, u32 newPath,
                                             u32 newPathLen)
{
	(void)ctx;
	struct HostWasiFd *oldDir = HostWasi_GetFd(oldDirFd);
	struct HostWasiFd *newDir = HostWasi_GetFd(newDirFd);
	char oldHostPath[4096];
	char newHostPath[4096];
	u32 err;

	if ((oldDir == NULL) || (newDir == NULL))
	{
		return WASI_ERRNO_BADF;
	}

	if (((err = HostWasi_CopyPath(oldHostPath, sizeof(oldHostPath), oldPath, oldPathLen)) != WASI_ERRNO_SUCCESS) ||
	    ((err = HostWasi_CopyPath(newHostPath, sizeof(newHostPath), newPath, newPathLen)) != WASI_ERRNO_SUCCESS))
	{
		return err;
	}

	return (renameat(oldDir->hostFd, oldHostPath, newDir->hostFd, newHostPath) == 0) ? WASI_ERRNO_SUCCESS : HostWasi_Errno(errno);
}
