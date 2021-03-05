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
        case PEEKCMD_FAV:
        case PEEKCMD_RECENT:
            return (info->stacklen == 2) ? 1 : 0;
        
        case PEEKCMD_ALPHA:
        case PEEKCMD_YEAR:
        case PEEKCMD_GENRE:
            return (info->stacklen == 3) ? 1 : 0;

        case PEEKCMD_ROOT:
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

static void peek_readdir_fav(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    (void) info;

    DIR *dp;
	if ((dp = opendir(_srcpath)) == NULL)
		return;
    
    int fd = dirfd(dp);

    if (!dbtxnopen(&_db, 1))
    {
        int rc;
        if (!dbcuropen(&_db))
        {
            char key[BUFFER_SIZE];
            sprintf(key, "fav/%s", _corename);

            MDB_val dbkey = {strlen(key) + 1, key};
            MDB_val dbdata;
            struct stat st;

            if (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET_RANGE)))
            {
                do
                {
                    char *filename = (char *)dbdata.mv_data;
                    if (!fstatat(fd, filename, &st, 0))
                    {
                        if (S_ISREG(st.st_mode))
                        {
                            if (filler(buf, filename, &st, 0))
                                break;
                        }
                    }
                }
                while (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_NEXT_DUP)));
            }

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }

    closedir(dp);
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

        case PEEKCMD_FAV:
            peek_readdir_fav(&info, buf, filler);
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
        case PEEKCMD_RECENT:
        case PEEKCMD_YEAR:
        case PEEKCMD_GENRE:
            break;
    }

    peek_parsepathrelease(&info);

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

    int fd;
    if ((fd = open(info.filepath, fi->flags)) == -1)
        return -errno;

    fi->fh = fd;

    return 0;
}

static int peek_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
    printf("peek_read: %s\n", path);

    (void) path;

    int res;
    if ((res = pread(fi->fh, buf, size, offset)) == -1)
        res = -errno;

    return res;
}

static int peek_release(const char *path, struct fuse_file_info *fi)
{
    printf("peek_release: %s\n", path);

    (void) path;
    close(fi->fh);

	return 0;
}

static struct fuse_operations peek_oper = {
	.getattr	= peek_getattr,
	.readdir	= peek_readdir,
	.open		= peek_open,
	.read		= peek_read,
	.release	= peek_release
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