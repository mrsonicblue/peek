#define FUSE_USE_VERSION 26

#include <config.h>

#define _XOPEN_SOURCE 700

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <db.h>
#include <path.h>

#define BUFFER_SIZE 4096

enum peekcmd
{
    PEEKCMD_NONE = -1,
    PEEKCMD_ROOT,
    PEEKCMD_FAV,
    PEEKCMD_RECENT,
    PEEKCMD_ALPHA,
    PEEKCMD_YEAR,
    PEEKCMD_GENRE
};

struct PathInfo
{
    char *stack[20];
    int stacklen;
    enum peekcmd cmd;
    int isfile;
    char *filepath;
};

static const char *__favpath = "Favorites";
static const char *__recentpath = "Recently Played";
static const char *__alphapath = "A-Z";
static const char *__yearpath = "Years";
static const char *__genrepath = "Genres";

static struct Database _db;
static char *_mountpath;
static char *_srcpath;
static char *_corename;

static void peek_parsepathrelease(struct PathInfo *info)
{
    int i;
    for (i = info->stacklen - 1; i >= 0; i--)
    {
        free(info->stack[i]);
    }

    if (info->filepath)
        free(info->filepath);
}

static int peek_isfile(struct PathInfo *info)
{
    switch (info->cmd)
    {
        case PEEKCMD_ROOT:
        case PEEKCMD_FAV:
        case PEEKCMD_RECENT:
        case PEEKCMD_YEAR:
        case PEEKCMD_GENRE:
            break;
        
        case PEEKCMD_ALPHA:
            switch (info->stacklen)
            {
                case 3:
                    return 1;
            }
            break;

        case PEEKCMD_NONE:
            break;
    }

    return 0;
}

static char *peek_filepath(struct PathInfo *info)
{
    char buf[BUFFER_SIZE];
    sprintf(buf, "%s/%s", _srcpath, info->stack[info->stacklen - 1]);

    char *result = malloc(strlen(buf) + 1);
    strcpy(result, buf);

    return result;
}

static int peek_parsepath(struct PathInfo *info, const char *path)
{
    memset(info, 0, sizeof(struct PathInfo));

    char *r = NULL;
    char *t;
    char *tmp;
    int pos = 0;
    for (t = strtok_r((char *)path, "/", &r); t != NULL; t = strtok_r(NULL, "/", &r))
    {
        tmp = malloc(strlen(t) + 1);
        strcpy(tmp, t);
        
        info->stack[pos++] = tmp;
    }
    info->stacklen = pos;

    enum peekcmd cmd = PEEKCMD_NONE;
    if (info->stacklen == 0)
    {
        cmd = PEEKCMD_ROOT;
    }
    else
    {
        char *first = info->stack[0];
        if (strcmp(first, __favpath) == 0)
        {
            cmd = PEEKCMD_FAV;
        }
        else if (strcmp(first, __recentpath) == 0)
        {
            cmd = PEEKCMD_RECENT;
        }
        else if (strcmp(first, __alphapath) == 0)
        {
            cmd = PEEKCMD_ALPHA;
        }
        else if (strcmp(first, __yearpath) == 0)
        {
            cmd = PEEKCMD_YEAR;
        }
        else if (strcmp(first, __genrepath) == 0)
        {
            cmd = PEEKCMD_GENRE;
        }
    }

    if (cmd == PEEKCMD_NONE)
    {
        peek_parsepathrelease(info);
        return -1;
    }

    info->cmd = cmd;
    if ((info->isfile = peek_isfile(info)))
        info->filepath = peek_filepath(info);

    return 0;
}

static int peek_getattr_fakedir(struct PathInfo *info, struct stat *stbuf)
{
    (void) info;

    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 1;

    return 0;
}

static int peek_getattr_file(struct PathInfo *info, struct stat *stbuf)
{
    int res;
	if ((res = lstat(info->filepath, stbuf)) == -1)
		return -errno;

    return 0;
}

static int peek_getattr(const char *path, struct stat *stbuf)
{
    //printf("peek_getattr: %s\n", path);

    struct PathInfo info;
    if (peek_parsepath(&info, path))
        return -ENOENT;

    int res;
    if (info.isfile)
        res = peek_getattr_file(&info, stbuf);
    else
        res = peek_getattr_fakedir(&info, stbuf);

    peek_parsepathrelease(&info);

    return res;
}

static int peek_access(const char *path, int mask)
{
    printf("peek_access: %s\n", path);
	int res;

	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_readlink(const char *path, char *buf, size_t size)
{
    printf("peek_readlink: %s\n", path);
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

static void peek_fakefill(void *buf, const char *name, fuse_fill_dir_t filler)
{
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_mode = S_IFDIR;

    filler(buf, name, &st, 0);
}

static void peek_readdir_root(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    (void) info;

    peek_fakefill(buf, __favpath, filler);
    peek_fakefill(buf, __recentpath, filler);
    peek_fakefill(buf, __alphapath, filler);
    peek_fakefill(buf, __yearpath, filler);
    peek_fakefill(buf, __genrepath, filler);
}

static void peek_readdir_alpha1(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    (void) info;

    peek_fakefill(buf, "0-9", filler);

    char letter[2];
    letter[1] = '\0';
    for (letter[0] = 'A'; letter[0] <= 'Z'; letter[0]++)
    {
        peek_fakefill(buf, letter, filler);
    }
}

static void peek_readdir_alpha2(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    (void) info;

	DIR *dp;
	if ((dp = opendir(_srcpath)) == NULL)
		return;

    char letter1 = info->stack[1][0];
    char letter2;
    if (letter1 >= 'A' && letter1 <= 'Z')
    {
        letter2 = letter1 + ('a' - 'A');
    }
    else
    {
        letter1 = '0';
        letter2 = '0';
    }

	struct dirent *de;
	while ((de = readdir(dp)) != NULL)
    {
        if (de->d_type == 8 /* DT_REG */)
        {
            char first = de->d_name[0];
            if (letter1 == '0')
            {
                if ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z'))
                    continue;
            }
            else
            {
                if (first != letter1 && first != letter2)
                    continue;
            }

            struct stat st;
            memset(&st, 0, sizeof(st));
            st.st_ino = de->d_ino;
            st.st_mode = de->d_type << 12;

            if (filler(buf, de->d_name, &st, 0))
                break;
        }
	}

	closedir(dp);
}

static int peek_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
    //printf("peek_readdir: %s\n", path);

	(void) offset;
	(void) fi;

    struct PathInfo info;
    if (peek_parsepath(&info, path))
        return -ENOENT;

    peek_fakefill(buf, ".", filler);
    peek_fakefill(buf, "..", filler);

    switch (info.cmd)
    {
        case PEEKCMD_ROOT:
            peek_readdir_root(&info, buf, filler);
            break;
        
        case PEEKCMD_ALPHA:
            switch (info.stacklen)
            {
                case 1:
                    peek_readdir_alpha1(&info, buf, filler);
                    break;

                case 2:
                    peek_readdir_alpha2(&info, buf, filler);
                    break;
            }
            break;

        case PEEKCMD_NONE:
        case PEEKCMD_FAV:
        case PEEKCMD_RECENT:
        case PEEKCMD_YEAR:
        case PEEKCMD_GENRE:
            break;
    }

    peek_parsepathrelease(&info);

    return 0;
}

static int peek_mknod(const char *path, mode_t mode, dev_t rdev)
{
    printf("peek_mknod: %s\n", path);
	int res;

	res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_mkdir(const char *path, mode_t mode)
{
    printf("peek_mkdir: %s\n", path);
	int res;

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_unlink(const char *path)
{
    printf("peek_unlink: %s\n", path);
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_rmdir(const char *path)
{
    printf("peek_rmdir: %s\n", path);
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_symlink(const char *from, const char *to)
{
    printf("peek_symlink: %s -> %s\n", from, to);
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_rename(const char *from, const char *to)
{
    printf("peek_rename: %s -> %s\n", from, to);
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_link(const char *from, const char *to)
{
    printf("peek_link: %s -> %s\n", from, to);
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_chmod(const char *path, mode_t mode)
{
    printf("peek_chmod: %s\n", path);
	int res;

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_chown(const char *path, uid_t uid, gid_t gid)
{
    printf("peek_chown: %s\n", path);
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_truncate(const char *path, off_t size)
{
    printf("peek_truncate: %s\n", path);
	int res;

	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_utimens(const char *path, const struct timespec ts[2])
{
    printf("peek_utimens: %s\n", path);
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_open(const char *path, struct fuse_file_info *fi)
{
    printf("peek_open: %s\n", path);

    struct PathInfo info;
    if (peek_parsepath(&info, path))
        return -ENOENT;

    if (!info.isfile)
        return -ENOENT;

    int res;
    if ((res = open(info.filepath, fi->flags)) == -1)
        return -errno;

    close(res);
    return 0;
}

static int peek_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
    printf("peek_read: %s\n", path);

	(void) fi;

    struct PathInfo info;
    if (peek_parsepath(&info, path))
        return -ENOENT;

    if (!info.isfile)
        return -ENOENT;

    int fd;
    if ((fd = open(info.filepath, O_RDONLY)) == -1)
        return -errno;

    int res;
    if ((res = pread(fd, buf, size, offset)) == -1)
        res = -errno;

    close(fd);
    return res;
}

static int peek_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
    printf("peek_write: %s\n", path);
	int fd;
	int res;

	(void) fi;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	return res;
}

static int peek_statfs(const char *path, struct statvfs *stbuf)
{
    printf("peek_statfs: %s\n", path);
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int peek_release(const char *path, struct fuse_file_info *fi)
{
    printf("peek_release: %s\n", path);
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) fi;
	return 0;
}

static int peek_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
    printf("peek_fsync: %s\n", path);
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

static int peek_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
    printf("peek_fallocate: %s\n", path);
	int fd;
	int res;

	(void) fi;

	if (mode)
		return -EOPNOTSUPP;

	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	close(fd);
	return res;
}

/* xattr operations are optional and can safely be left unimplemented */
static int peek_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
    printf("peek_setxattr: %s\n", path);
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int peek_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
    printf("peek_getxattr: %s\n", path);
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int peek_listxattr(const char *path, char *list, size_t size)
{
    printf("peek_listxattr: %s\n", path);
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int peek_removexattr(const char *path, const char *name)
{
    printf("peek_removexattr: %s\n", path);
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}

static struct fuse_operations peek_oper = {
	.getattr	= peek_getattr,
	.access		= peek_access,
	.readlink	= peek_readlink,
	.readdir	= peek_readdir,
	.mknod		= peek_mknod,
	.mkdir		= peek_mkdir,
	.symlink	= peek_symlink,
	.unlink		= peek_unlink,
	.rmdir		= peek_rmdir,
	.rename		= peek_rename,
	.link		= peek_link,
	.chmod		= peek_chmod,
	.chown		= peek_chown,
	.truncate	= peek_truncate,
	.utimens	= peek_utimens,
	.open		= peek_open,
	.read		= peek_read,
	.write		= peek_write,
	.statfs		= peek_statfs,
	.release	= peek_release,
	.fsync		= peek_fsync,
	.fallocate	= peek_fallocate,
	.setxattr	= peek_setxattr,
	.getxattr	= peek_getxattr,
	.listxattr	= peek_listxattr,
	.removexattr	= peek_removexattr,
};

static int initialize(void)
{
    if (dbopen(&_db))
        return -1;
    
    return 0;
}

static void cleanup(void)
{
    dbclose(&_db);
}

static int fuse_main_peek(int argc, char *argv[], const struct fuse_operations *op, size_t op_size, void *user_data)
{
	struct fuse *fuse;
	char *mountpoint;
	int multithreaded;
	int res;

	fuse = fuse_setup(argc, argv, op, op_size, &mountpoint, &multithreaded, user_data);
	if (fuse == NULL)
    {
        printf("Fuse setup failed\n");
		return 1;
    }

    printf("Mount path: %s\n", mountpoint);

    int mountlen = strlen(mountpoint);
    _mountpath = malloc(mountlen + 1);
    strcpy(_mountpath, mountpoint);

    if ((_srcpath = pathup(_mountpath)))
    {
        printf("Source path: %s\n", _srcpath);

        if ((_corename = pathfile(_srcpath)))
        {
            printf("Core name: %s\n", _corename);

            if (multithreaded)
                res = fuse_loop_mt(fuse);
            else
                res = fuse_loop(fuse);
        }
        else
        {
            printf("Failed to get core name\n");
            res = -1;
        }
    }
    else
    {
        printf("Failed to get source path\n");
        res = -1;
    }

	fuse_teardown(fuse, mountpoint);
	if (res == -1)
		return 1;

	return 0;
}

int main(int argc, char *argv[])
{
    printf("Starting up...\n");

    if (initialize())
        return 1;

    umask(0);
    int err = fuse_main_peek(argc, argv, &peek_oper, sizeof(peek_oper), NULL);

    printf("\n");
    printf("Cleaning up!\n");

    cleanup();

    printf("All done!\n");

	return err ? 1 : 0;
}