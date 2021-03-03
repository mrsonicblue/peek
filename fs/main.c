/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall hello_ll.c `pkg-config fuse --cflags --libs` -o hello_ll
*/

#define FUSE_USE_VERSION 26

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <lmdb.h>

#define BUFFER_SIZE 4096

struct Database
{
    struct MDB_env *env;
    MDB_dbi dbi;
    MDB_txn *txn;
    bool txnreadonly;
    MDB_cursor *cur;
};

static const char *hello_str = "Hello World!\n";
static const char *hello_name = "hello";

struct Database _db;

static bool dbtxnopen(bool readonly)
{
    if (_db.txn)
    {
        printf("Transaction already open\n");
        return false;
    }

    int rc;
    int flags = readonly ? MDB_RDONLY : 0;
    if ((rc = mdb_txn_begin(_db.env, NULL, flags, &_db.txn))) 
    {
        printf("Failed to create transaction: %d\n", rc);
        return false;
    }

    _db.txnreadonly = readonly;

    return true;
}

static bool dbtxncheck(void)
{
    if (!_db.txn)
    {
        printf("No open transaction\n");
        return false;
    }

    return true;
}

static bool dbtxnclose(void)
{
    if (!dbtxncheck())
        return false;

    int rc;
    if (_db.txnreadonly)
    {
        mdb_txn_abort(_db.txn);
    }
    else
    {
        if ((rc = mdb_txn_commit(_db.txn)))
        {
            printf("Failed to commit transaction: %d\n", rc);
            return false;
        }
    }

    _db.txn = NULL;

    return true;
}

static bool dbcuropen(void)
{
    if (_db.cur)
    {
        printf("Cursor already open\n");
        return false;
    }

    int rc;
    if ((rc = mdb_cursor_open(_db.txn, _db.dbi, &_db.cur)))
    {
        printf("Failed to open cursor: %d\n", rc);
        return false;
    }

    return true;
}

static bool dbcurcheck(void)
{
    if (!_db.cur)
    {
        printf("No open cursor\n");
        return false;
    }

    return true;
}

static bool dbcurclose(void)
{
    if (!dbcurcheck())
        return false;

    mdb_cursor_close(_db.cur);
    _db.cur = NULL;

    return true;
}

static bool dbput(char *key, char *data)
{
    if (!dbtxncheck())
        return false;
    
    MDB_val dbkey = {strlen(key) + 1, key};
    MDB_val dbdata = {strlen(data) + 1, data};

    int rc;
    if ((rc = mdb_put(_db.txn, _db.dbi, &dbkey, &dbdata, MDB_NODUPDATA)))
    {
        if (rc != MDB_KEYEXIST)
        {
            printf("Failed to write data: %d\n", rc);
            return false;
        }
    }

    return true;
}

static bool dbdel(char *key, char *data)
{
    if (!dbtxncheck())
        return false;

    MDB_val dbkey = {strlen(key) + 1, key};
    MDB_val dbdata;
    if (data)
    {
        dbdata.mv_size = strlen(data) + 1;
        dbdata.mv_data = data;
    }

    int rc;
    if ((rc = mdb_del(_db.txn, _db.dbi, &dbkey, data ? &dbdata : NULL)))
    {
        if (rc != MDB_NOTFOUND)
        {
            printf("Failed to delete data: %d\n", rc);
            return false;
        }
    }

    return true;
}

static int hello_stat(fuse_ino_t ino, struct stat *stbuf)
{
    printf("stat\n");

	stbuf->st_ino = ino;
	switch (ino) {
	case 1:
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		break;

	case 2:
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(hello_str);
		break;

	default:
		return -1;
	}
	return 0;
}

static void hello_ll_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
    printf("getattr\n");

	struct stat stbuf;

	(void) fi;

	memset(&stbuf, 0, sizeof(stbuf));
	if (hello_stat(ino, &stbuf) == -1)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_attr(req, &stbuf, 1.0);
}

static void hello_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    printf("lookup\n");

	struct fuse_entry_param e;

	if (parent != 1 || strcmp(name, hello_name) != 0)
		fuse_reply_err(req, ENOENT);
	else {
		memset(&e, 0, sizeof(e));
		e.ino = 2;
		e.attr_timeout = 1.0;
		e.entry_timeout = 1.0;
		hello_stat(e.ino, &e.attr);

		fuse_reply_entry(req, &e);
	}
}

struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
		       fuse_ino_t ino)
{
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
	b->p = (char *) realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
			  b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
			     off_t off, size_t maxsize)
{
	if (off < bufsize)
		return fuse_reply_buf(req, buf + off,
				      min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}

static void hello_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
    printf("readdir\n");

	(void) fi;

	if (ino != 1)
		fuse_reply_err(req, ENOTDIR);
	else {
		struct dirbuf b;

		memset(&b, 0, sizeof(b));
		dirbuf_add(req, &b, ".", 1);
		dirbuf_add(req, &b, "..", 1);

        if (dbtxnopen(true))
        {
            int rc;

            if (dbcuropen())
            {
                char *key = "TESTING";
                MDB_val dbkey = {strlen(key) + 1, key};
                MDB_val dbdata;

                if ((rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET_RANGE)))
                {
                    printf("No data found: %d\n", rc);
                }
                else
                {
                    printf("Data found!\n");
                    do
                    {
                        printf("Data: %s\n", (char *)dbdata.mv_data);
                        dirbuf_add(req, &b, (char *)dbdata.mv_data, 2);
                    }
                    while (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_NEXT)));
                }

                dbcurclose();
            }

            dbtxnclose();
        }

		//dirbuf_add(req, &b, hello_name, 2);

		reply_buf_limited(req, b.p, b.size, off, size);
		free(b.p);
	}
}

static void hello_ll_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
    printf("open\n");

	if (ino != 2)
		fuse_reply_err(req, EISDIR);
	else if ((fi->flags & 3) != O_RDONLY)
		fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}

static void hello_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
    printf("read\n");

	(void) fi;

	assert(ino == 2);
	reply_buf_limited(req, hello_str, strlen(hello_str), off, size);
}

static struct fuse_lowlevel_ops hello_ll_oper = {
	.lookup		= hello_ll_lookup,
	.getattr	= hello_ll_getattr,
	.readdir	= hello_ll_readdir,
	.open		= hello_ll_open,
	.read		= hello_ll_read,
};

static bool initialize(void)
{
    int rc;

    char selfpath[BUFFER_SIZE];
    readlink("/proc/self/exe", selfpath, BUFFER_SIZE);

    char *lastslash;
    if ((lastslash = strrchr(selfpath, '/')) == NULL)
    {
        printf("Failed to find program path");
        return false;
    }
    *lastslash = 0;

    printf("Program directory: %s\n", selfpath);

    char dbpath[BUFFER_SIZE];
    sprintf(dbpath, "%s/data", selfpath);

    printf("Database path: %s\n", dbpath);

    struct stat st = {0};
    if (stat(dbpath, &st) == -1)
    {
        if ((rc = mkdir(dbpath, 0775)))
        {
            printf("Failed to create database directory\n");
            return false;
        }
    }

    if ((rc = mdb_env_create(&_db.env)))
    {
        printf("Failed to create database environment: %d\n", rc);
        return false;
    }

    // Use default configuration for now
    //mdb_env_set_maxdbs(_db.env, 5);
    //mdb_env_set_mapsize(_db.env, (size_t)1048576 * (size_t)50); // 1MB * 50

    if ((rc = mdb_env_open(_db.env, dbpath, 0, 0664)))
    {
        printf("Failed to open database environment: %d\n", rc);
        return false;
    }

    if (dbtxnopen(false))
    {
        if ((rc = mdb_dbi_open(_db.txn, NULL, MDB_DUPSORT | MDB_CREATE, &_db.dbi)))
        {
            printf("Failed to open database: %d\n", rc);
            return false;
        }

        dbtxnclose();
    }

	return true;
}

static void cleanup(void)
{
    mdb_dbi_close(_db.env, _db.dbi);
    mdb_env_close(_db.env);
}

int main(int argc, char *argv[])
{
    if (!initialize())
        return 1;

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_chan *ch;
    char *mountpoint;
    int err = -1;

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
        (ch = fuse_mount(mountpoint, &args)) != NULL) {
        struct fuse_session *se;

        se = fuse_lowlevel_new(&args, &hello_ll_oper,
                        sizeof(hello_ll_oper), NULL);
        if (se != NULL) {
            if (fuse_set_signal_handlers(se) != -1) {
                fuse_session_add_chan(se, ch);
                err = fuse_session_loop(se);
                fuse_remove_signal_handlers(se);
                fuse_session_remove_chan(ch);
            }
            fuse_session_destroy(se);
        }
        fuse_unmount(mountpoint, ch);
    }
    fuse_opt_free_args(&args);

    printf("\n");
    printf("Cleaning up!\n");

    cleanup();

    printf("All done!\n");

	return err ? 1 : 0;
}