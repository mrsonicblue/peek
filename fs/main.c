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
    PEEKCMD_ROOT,
    PEEKCMD_FAV,
    PEEKCMD_REC,
    PEEKCMD_ALPHA,
    PEEKCMD_HAS,
    PEEKCMD_MANAGE
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
static const char *__recpath = "Recently Played";
static const char *__alphapath = "A-Z";
static const char *__managepath = "~ Manage Data";
static const char *__managefav = "Favorite";
static const char *__manageyay = "Updated!";

static struct Database _db;
static char *_mountpath;
static char *_srcpath;
static char *_corename;

static char *trimcheck(char *s, int *c)
{
    // Search for "[ ] " or "[X] " at beginning of string and remove if present
    if (strlen(s) >= 4)
    {
        if (s[0] == '[' && (s[1] == ' ' || s[1] == 'X') && s[2] == ']' && s[3] == ' ')
        {
            *c = (s[1] == 'X') ? 1 : 0;
            return s + 4;
        }
    }

    *c = -1;
    return s;
}

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
        case PEEKCMD_REC:
            return (info->stacklen == 2) ? 1 : 0;
        
        case PEEKCMD_ALPHA:
        case PEEKCMD_HAS:
            return (info->stacklen == 3) ? 1 : 0;

        case PEEKCMD_ROOT:
        case PEEKCMD_MANAGE:
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

    char pathdup[BUFFER_SIZE];
    strcpy(pathdup, path);

    // Skip leading slash in path
    char *pathbeg = pathdup;
    if (*pathbeg == '/')
        pathbeg++;

    char *r = NULL;
    char *t;
    char *tmp;
    int pos = 0;
    for (t = strtokplus(pathbeg, '/', &r); t != NULL; t = strtokplus(NULL, '/', &r))
    {
        tmp = malloc(strlen(t) + 1);
        strcpy(tmp, t);
        
        info->stack[pos++] = tmp;
    }
    info->stacklen = pos;

    enum peekcmd cmd;
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
        else if (strcmp(first, __recpath) == 0)
        {
            cmd = PEEKCMD_REC;
        }
        else if (strcmp(first, __alphapath) == 0)
        {
            cmd = PEEKCMD_ALPHA;
        }
        else if (strcmp(first, __managepath) == 0)
        {
            cmd = PEEKCMD_MANAGE;
        }
        else
        {
            cmd = PEEKCMD_HAS;
        }
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

static void peek_readdir_filekey(struct PathInfo *info, void *buf, fuse_fill_dir_t filler, char *filekey, int valueoffset)
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
            MDB_val dbkey = {strlen(filekey) + 1, filekey};
            MDB_val dbdata;
            struct stat st;

            if (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET)))
            {
                do
                {
                    if (dbdata.mv_size > valueoffset)
                    {
                        char *filename = (char *)dbdata.mv_data + valueoffset;
                        if (!fstatat(fd, filename, &st, 0))
                        {
                            if (S_ISREG(st.st_mode))
                            {
                                if (filler(buf, filename, &st, 0))
                                    break;
                            }
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

static void peek_readdir_dbslice(struct PathInfo *info, void *buf, fuse_fill_dir_t filler, char *prefix, char *checkfile)
{
    (void) info;

    size_t prefixlen = strlen(prefix);
    char slice[BUFFER_SIZE];
    size_t slicelen = 0;

    if (!dbtxnopen(&_db, 1))
    {
        if (!dbcuropen(&_db))
        {
            int rc;
            MDB_cursor *checkcur = NULL;
            MDB_val dbfile;
            if (checkfile)
            {
                dbfile.mv_size = strlen(checkfile) + 1;
                dbfile.mv_data = checkfile;

                if ((rc = mdb_cursor_open(_db.txn, _db.dbfil, &checkcur)))
                {
                    printf("Failed to open cursor: %d\n", rc);
                    return;
                }
            }

            MDB_val dbkey = {prefixlen + 1, prefix};
            MDB_val dbdata;

            if (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET_RANGE)))
            {
                do
                {
                    if (prefixlen > dbkey.mv_size || memcmp(prefix, dbkey.mv_data, prefixlen) != 0)
                        break;
                    
                    char *curstart = (char *)dbkey.mv_data + prefixlen;
                    char *curend = strchr(curstart, '/');
                    size_t curlen = curend ? (size_t)(curend - curstart) : (dbkey.mv_size - 1 - prefixlen);

                    if (slicelen != curlen || memcmp(slice, curstart, slicelen) != 0)
                    {
                        int sliceindex = 0;

                        if (checkfile)
                        {
                            int has = !(rc = mdb_cursor_get(_db.cur, &dbkey, &dbfile, MDB_GET_BOTH));

                            slice[0] = '[';
                            slice[1] = has ? 'X' : ' ';
                            slice[2] = ']';
                            slice[3] = ' ';
                            sliceindex = 4;
                        }

                        memcpy(slice + sliceindex, curstart, curlen);
                        slice[curlen + sliceindex] = '\0';
                        slicelen = curlen;

                        peek_fakefill(buf, slice, filler);
                    }
                }
                while (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_NEXT_NODUP)));
            }

            if (checkfile)
                mdb_cursor_close(checkcur);

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }
}

static void peek_readdir_root(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    (void) info;

    peek_fakefill(buf, __favpath, filler);
    peek_fakefill(buf, __recpath, filler);
    peek_fakefill(buf, __alphapath, filler);
    peek_fakefill(buf, __managepath, filler);

    char prefix[BUFFER_SIZE];
    sprintf(prefix, "has/%s/", _corename);
    peek_readdir_dbslice(info, buf, filler, prefix, NULL);
}

static void peek_readdir_fav(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    char filekey[BUFFER_SIZE];
    sprintf(filekey, "fav/%s", _corename);

    peek_readdir_filekey(info, buf, filler, filekey, 0);
}

static void peek_readdir_alpha_root(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
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

static void peek_readdir_alpha_letter(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
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

static void peek_readdir_rec(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    char filekey[BUFFER_SIZE];
    sprintf(filekey, "rec/%s", _corename);

    peek_readdir_filekey(info, buf, filler, filekey, TIME_LEN);
}

static void peek_readdir_has_level1(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    (void) info;

    char prefix[BUFFER_SIZE];
    sprintf(prefix, "has/%s/%s/", _corename, info->stack[0]);
    peek_readdir_dbslice(info, buf, filler, prefix, NULL);
}

static void peek_readdir_has_level2(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    char filekey[BUFFER_SIZE];
    sprintf(filekey, "has/%s/%s/%s", _corename, info->stack[0], info->stack[1]);
    peek_readdir_filekey(info, buf, filler, filekey, 0);
}

static void peek_readdir_manage_root(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    (void) info;

    DIR *dp;
	if ((dp = opendir(_srcpath)) == NULL)
		return;
    
	struct dirent *de;
	while ((de = readdir(dp)) != NULL)
    {
        if (de->d_type == 8 /* DT_REG */)
        {
            peek_fakefill(buf, de->d_name, filler);
        }
	}

    closedir(dp);
}

static void peek_readdir_manage_file(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    char *file = info->stack[1];

    // Read if favorite
    if (!dbtxnopen(&_db, 1))
    {
        int rc;

        if (!dbcuropen(&_db))
        {
            char tmp[BUFFER_SIZE];
            sprintf(tmp, "fav/%s", _corename);

            MDB_val dbkey = {strlen(tmp) + 1, tmp};
            MDB_val dbdata = {strlen(file) + 1, file};

            int fav = !(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_GET_BOTH));

            sprintf(tmp, "[%c] %s", fav ? 'X' : ' ', __managefav);
            peek_fakefill(buf, tmp, filler);

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }

    // Read level 1 filters
    char prefix[BUFFER_SIZE];
    sprintf(prefix, "has/%s/", _corename);
    peek_readdir_dbslice(info, buf, filler, prefix, NULL);
}

static void peek_readdir_manage_yay(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    (void) info;
    
    peek_fakefill(buf, __manageyay, filler);
}

static void peek_readdir_manage_setfav(struct PathInfo *info, void *buf, fuse_fill_dir_t filler, int checked)
{
    char *file = info->stack[1];

    if (!dbtxnopen(&_db, 0))
    {
        if (!dbcuropen(&_db))
        {
            char tmp[BUFFER_SIZE];
            sprintf(tmp, "fav/%s", _corename);

            if (checked == 1)
                dbdel(&_db, tmp, file);
            else
                dbput(&_db, tmp, file);

            peek_readdir_manage_yay(info, buf, filler);

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }
}

static void peek_readdir_manage_sethas(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    char *file = info->stack[1];
    char *level1 = info->stack[2];

    int checked;
    char *level2 = trimcheck(info->stack[3], &checked);

    if (!dbtxnopen(&_db, 0))
    {
        if (!dbcuropen(&_db))
        {
            char tmp[BUFFER_SIZE];
            sprintf(tmp, "has/%s/%s/%s", _corename, level1, level2);

            if (checked == 1)
                dbdel(&_db, tmp, file);
            else
                dbput(&_db, tmp, file);

            peek_readdir_manage_yay(info, buf, filler);

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }
}

static void peek_readdir_manage_level3(struct PathInfo *info, void *buf, fuse_fill_dir_t filler)
{
    int checked;
    char *level = trimcheck(info->stack[2], &checked);

    if (strcmp(level, __managefav) == 0)
    {
        peek_readdir_manage_setfav(info, buf, filler, checked);
    }
    else
    {
        char *file = info->stack[1];

        char prefix[BUFFER_SIZE];
        sprintf(prefix, "has/%s/%s/", _corename, level);
        peek_readdir_dbslice(info, buf, filler, prefix, file);
    }
}

static int peek_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
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
                    peek_readdir_alpha_root(&info, buf, filler);
                    break;

                case 2:
                    peek_readdir_alpha_letter(&info, buf, filler);
                    break;
            }
            break;

        case PEEKCMD_REC:
            peek_readdir_rec(&info, buf, filler);
            break;

        case PEEKCMD_HAS:
            switch (info.stacklen)
            {
                case 1:
                    peek_readdir_has_level1(&info, buf, filler);
                    break;

                case 2:
                    peek_readdir_has_level2(&info, buf, filler);
                    break;
            }
            break;

        case PEEKCMD_MANAGE:
            switch (info.stacklen)
            {
                case 1:
                    peek_readdir_manage_root(&info, buf, filler);
                    break;

                case 2:
                    peek_readdir_manage_file(&info, buf, filler);
                    break;

                case 3:
                    peek_readdir_manage_level3(&info, buf, filler);
                    break;

                case 4:
                    peek_readdir_manage_sethas(&info, buf, filler);
                    break;
            }
            break;
    }

    peek_parsepathrelease(&info);

    return 0;
}

static int peek_open(const char *path, struct fuse_file_info *fi)
{
    //printf("peek_open: %s\n", path);

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

static int peek_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    //printf("peek_read: %s\n", path);

    (void) path;

    int res;
    if ((res = pread(fi->fh, buf, size, offset)) == -1)
        res = -errno;

    return res;
}

static int peek_release(const char *path, struct fuse_file_info *fi)
{
    //printf("peek_release: %s\n", path);

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