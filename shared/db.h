#include <lmdb.h>

struct Database
{
    struct MDB_env *env;
    MDB_dbi dbfil;
    MDB_dbi dbstr;
    MDB_txn *txn;
    int txnreadonly;
    MDB_cursor *cur;
};

#define TIME_LEN 8

int dbopen(struct Database *db);
void dbclose(struct Database *db);
int dbtxnopen(struct Database *db, int readonly);
int dbtxncheck(struct Database *db);
int dbtxnclose(struct Database *db);
int dbcuropen(struct Database *db);
int dbcurcheck(struct Database *db);
int dbcurclose(struct Database *db);
int dbput(struct Database *db, char *key, char *data);
int dbdel(struct Database *db, char *key, char *data);
int dbstrget(struct Database *db, char *str, unsigned int id);
int dbstrput(struct Database *db, char *str, unsigned int *id);
