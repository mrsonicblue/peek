#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "db.h"

#define BUFFER_SIZE 4096

int dbopen(struct Database *db)
{
    int rc;

    *db = (const struct Database){ 0 };

    char selfpath[BUFFER_SIZE];
    readlink("/proc/self/exe", selfpath, BUFFER_SIZE);

    char *lastslash;
    if ((lastslash = strrchr(selfpath, '/')) == NULL)
    {
        printf("Failed to find program path");
        return -1;
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
            return -1;
        }
    }

    if ((rc = mdb_env_create(&db->env)))
    {
        printf("Failed to create database environment: %d\n", rc);
        return -1;
    }

    // Use default configuration for now
    //mdb_env_set_maxdbs(db->env, 5);
    //mdb_env_set_mapsize(db->env, (size_t)1048576 * (size_t)50); // 1MB * 50

    if ((rc = mdb_env_open(db->env, dbpath, 0, 0664)))
    {
        printf("Failed to open database environment: %d\n", rc);
        return -1;
    }

    if (!dbtxnopen(db, 0))
    {
        if ((rc = mdb_dbi_open(db->txn, NULL, MDB_DUPSORT | MDB_CREATE, &db->dbi)))
        {
            printf("Failed to open database: %d\n", rc);
            return -1;
        }

        dbtxnclose(db);
    }

	return 0;
}

void dbclose(struct Database *db)
{
    mdb_dbi_close(db->env, db->dbi);
    mdb_env_close(db->env);
}

int dbtxnopen(struct Database *db, int readonly)
{
    if (db->txn)
    {
        printf("Transaction already open\n");
        return -1;
    }

    int rc;
    int flags = readonly ? MDB_RDONLY : 0;
    if ((rc = mdb_txn_begin(db->env, NULL, flags, &db->txn))) 
    {
        printf("Failed to create transaction: %d\n", rc);
        return -1;
    }

    db->txnreadonly = readonly;

    return 0;
}

int dbtxncheck(struct Database *db)
{
    if (!db->txn)
    {
        printf("No open transaction\n");
        return -1;
    }

    return 0;
}

int dbtxnclose(struct Database *db)
{
    if (dbtxncheck(db))
        return -1;

    int rc;
    if (db->txnreadonly)
    {
        mdb_txn_abort(db->txn);
    }
    else
    {
        if ((rc = mdb_txn_commit(db->txn)))
        {
            printf("Failed to commit transaction: %d\n", rc);
            return -1;
        }
    }

    db->txn = NULL;

    return 0;
}

int dbcuropen(struct Database *db)
{
    if (db->cur)
    {
        printf("Cursor already open\n");
        return -1;
    }

    int rc;
    if ((rc = mdb_cursor_open(db->txn, db->dbi, &db->cur)))
    {
        printf("Failed to open cursor: %d\n", rc);
        return -1;
    }

    return 0;
}

int dbcurcheck(struct Database *db)
{
    if (!db->cur)
    {
        printf("No open cursor\n");
        return -1;
    }

    return 0;
}

int dbcurclose(struct Database *db)
{
    if (dbcurcheck(db))
        return -1;

    mdb_cursor_close(db->cur);
    db->cur = NULL;

    return 0;
}

int dbput(struct Database *db, char *key, char *data)
{
    if (dbtxncheck(db))
        return -1;
    
    MDB_val dbkey = {strlen(key) + 1, key};
    MDB_val dbdata = {strlen(data) + 1, data};

    int rc;
    if ((rc = mdb_put(db->txn, db->dbi, &dbkey, &dbdata, MDB_NODUPDATA)))
    {
        if (rc != MDB_KEYEXIST)
        {
            printf("Failed to write data: %d\n", rc);
            return -1;
        }
    }

    return 0;
}

int dbdel(struct Database *db, char *key, char *data)
{
    if (dbtxncheck(db))
        return -1;

    MDB_val dbkey = {strlen(key) + 1, key};
    MDB_val dbdata;
    if (data)
    {
        dbdata.mv_size = strlen(data) + 1;
        dbdata.mv_data = data;
    }

    int rc;
    if ((rc = mdb_del(db->txn, db->dbi, &dbkey, data ? &dbdata : NULL)))
    {
        if (rc != MDB_NOTFOUND)
        {
            printf("Failed to delete data: %d\n", rc);
            return -1;
        }
    }

    return 0;
}