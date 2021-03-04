#include <lmdb.h>

struct Database
{
    struct MDB_env *env;
    MDB_dbi dbi;
    MDB_txn *txn;
    int txnreadonly;
    MDB_cursor *cur;
};

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