#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <paths.h>
#include <termios.h>
#include <lmdb.h>

#define GAMES_PATH "/media/fat/games"
#define BUFFER_SIZE 4096
#define EOM 4
#define EVENT_SIZE ( sizeof (struct inotify_event) )
#define EVENT_BUFFER_SIZE ( 4 * ( EVENT_SIZE + 16 ) )

struct Portal
{
    int fd;
    char *cmdstack[20];
    int cmdstackpos;
    char *readbuf;
    int readpos;
};

struct Database
{
    struct MDB_env *env;
    MDB_dbi dbi;
    MDB_txn *txn;
    bool txnreadonly;
    MDB_cursor *cur;
};

struct Notify
{
    int id;
    int watchcore;
    int watchroms;
    char *readbuf;
    struct Portal *portal;
};

static volatile bool _terminated;
char *_mbcpath;
char *_core;
char *_rom;
struct Database _db;

void shutdown()
{
	if (_terminated)
		return;

	_terminated = true;
}

bool setinterface(struct Portal *portal, int speed)
{
    struct termios tty;

    if (tcgetattr(portal->fd, &tty) < 0)
    {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return false;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    cfmakeraw(&tty);

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(portal->fd, TCSANOW, &tty) != 0)
    {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return false;
    }

    return true;
}

char *rtrim(char *s)
{
    char* back = s + strlen(s);
    while(isspace(*--back));
    *(back+1) = '\0';
    return s;
}

int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

bool dbtxnopen(bool readonly)
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

bool dbtxncheck()
{
    if (!_db.txn)
    {
        printf("No open transaction\n");
        return false;
    }

    return true;
}

bool dbtxnclose()
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

bool dbcuropen()
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

bool dbcurcheck()
{
    if (!_db.cur)
    {
        printf("No open cursor\n");
        return false;
    }

    return true;
}

bool dbcurclose()
{
    if (!dbcurcheck())
        return false;

    mdb_cursor_close(_db.cur);
    _db.cur = NULL;

    return true;
}

bool dbput(char *key, char *data)
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

bool dbdel(char *key, char *data)
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

void writeraw(struct Portal *portal, char *s, int sendlen)
{
    int writelen = write(portal->fd, s, sendlen);
    if (writelen != sendlen)
    {
        printf("Error from write: %d, %d\n", writelen, errno);
    }
    tcdrain(portal->fd); // wait for write
}

void writestr(struct Portal *portal, char *s)
{
    int sendlen = strlen(s) + 1;
    writeraw(portal, s, sendlen);
}

void writeeol(struct Portal *portal)
{
    char s[] = {0x00};
    printf("Sending EOL\n");
    writeraw(portal, s, 1);
}

void writeeom(struct Portal *portal)
{
    char s[] = {EOM};    
    printf("Sending EOM\n");
    writeraw(portal, s, 1);
}

void runcmd_proc(struct Portal *portal, char *cmdkey, char *path)
{
    char procbuf[BUFFER_SIZE];
    FILE *proc;

    printf("Running process: %s\n", path);

    if ((proc = popen(path, "r")) == NULL)
    {
        printf("Failed to open process\n");
    }
    else
    {
        writestr(portal, cmdkey);

        while (fgets(procbuf, BUFFER_SIZE, proc) != NULL)
        {
            // rtrim(procbuf);
            int len = strlen(procbuf);
            writeraw(portal, procbuf, len);
        }

        writeeol(portal);
        writeeom(portal);

        if (pclose(proc))
        {
            printf("Process exited with error status\n");
        }
    }
}

void runcmd_bproc(char *path)
{
    printf("Forking process\n");

    // To create a detached process, we need to orphan a
    // grandchild process. Start with child process.
    int pid1;
    if ((pid1 = fork()) < 0)
    {
        printf("Failed to fork process 1\n");
        return;
    }

    if (pid1 == 0)
    {
        // Then fork grandchild process
        int pid2;
        if ((pid2 = fork()) < 0)
        {
            printf("Failed to fork process 2\n");
            _exit(127);
        }

        if (pid2 == 0)
        {
            // The grandchild executes the background process
            printf("Running background process: %s\n", path);

            execl(_PATH_BSHELL, "sh", "-c", path, (char *)NULL);

            printf("Failed to execute background process\n");
            _exit(127);
        }
        else
        {
            // The child exits, which detaches the grandchild
            // from the parent
            _exit(0);
        }
    }
    else
    {
        // The parent waits until the child exits
        int status;
        waitpid(pid1, &status, 0);
    }
}

void runcmd_dbget(struct Portal *portal, char *cmdkey, char *key)
{
    if (dbtxnopen(true))
    {
        int rc;

        if (dbcuropen())
        {
            MDB_val dbkey = {strlen(key) + 1, key};
            MDB_val dbdata;

            writestr(portal, cmdkey);

            if ((rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET)))
            {
                printf("No data found: %d\n", rc);
            }
            else
            {
                printf("Data found!\n");

                do
                {
                    printf("Data: %s\n", (char *)dbdata.mv_data);
                    writestr(portal, (char *)dbdata.mv_data);
                }
                while (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_NEXT)));
            }

            writeeom(portal);

            dbcurclose();
        }

        dbtxnclose();
    }
}

void runcmd_dbchk(struct Portal *portal, char *cmdkey, char *key, char *data)
{
    if (dbtxnopen(true))
    {
        int rc;

        if (dbcuropen())
        {
            MDB_val dbkey = {strlen(key) + 1, key};
            MDB_val dbdata = {strlen(data) + 1, data};

            writestr(portal, cmdkey);

            if ((rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_GET_BOTH)))
            {
                writestr(portal, "0");
            }
            else
            {
                writestr(portal, "1");
            }

            writeeom(portal);

            dbcurclose();
        }

        dbtxnclose();
    }
}

void runcmd_dbput(struct Portal *portal, char *cmdkey, char *key, char *value)
{
    if (dbtxnopen(false))
    {
        dbput(key, value);

        dbtxnclose();
    }
}

void runcmd_dbdel(struct Portal *portal, char *cmdkey, char *key, char *value)
{
    if (dbtxnopen(false))
    {
        dbdel(key, value);

        dbtxnclose();
    }    
}

void runcmd(struct Portal *portal)
{
    char **stack = portal->cmdstack;
    int count = portal->cmdstackpos;

    if (count < 2)
    {
        printf("All commands requires at least two arguments\n");
        return;
    }
    
    if (strcmp(stack[1], "proc") == 0)
    {
        // Run process
        if (count < 3)
        {
            printf("'proc' command requires three arguments\n");
            return;
        }

        runcmd_proc(portal, stack[0], stack[2]);
    }
    else if (strcmp(stack[1], "bproc") == 0)
    {
        // Run background process
        if (count < 3)
        {
            printf("'bproc' command requires three arguments\n");
            return;
        }

        runcmd_bproc(stack[2]);
    }
    else if (strcmp(stack[1], "dbget") == 0)
    {
        // Get database records for key
        if (count < 3)
        {
            printf("'dbget' command requires three arguments\n");
            return;
        }

        runcmd_dbget(portal, stack[0], stack[2]);
    }
    else if (strcmp(stack[1], "dbchk") == 0)
    {
        // Check if database record exists
        if (count < 4)
        {
            printf("'dbchk' command requires four arguments\n");
            return;
        }

        runcmd_dbchk(portal, stack[0], stack[2], stack[3]);
    }
    else if (strcmp(stack[1], "dbput") == 0)
    {
        // Store database record
        if (count < 4)
        {
            printf("'dbput' command requires four arguments\n");
            return;
        }

        runcmd_dbput(portal, stack[0], stack[2], stack[3]);
    }
    else if (strcmp(stack[1], "dbdel") == 0)
    {
        // Delete database record(s)
        if (count < 3)
        {
            printf("'dbdel' command requires three or four arguments\n");
            return;
        }

        char *value = (count == 3) ? NULL : stack[3];
        runcmd_dbdel(portal, stack[0], stack[2], value);
    }
    else
    {
        printf("Unknown command type: %s\n", stack[1]);
    }
}

void readrom(struct Portal *portal, char *rom)
{
    if (strcmp(_rom, rom) == 0)
        return;
    
    if (strlen(_rom) > 0)
        free(_rom);

    _rom = malloc(strlen(rom) + 1);
    strcpy(_rom, rom);

    writestr(portal, "rom");
    writestr(portal, _rom);
    writeeom(portal);
}

void readcore(struct Portal *portal, struct Notify *notify)
{
    FILE *file;
    char core[BUFFER_SIZE];

    if ((file = fopen("/tmp/CORENAME", "r")) == NULL) 
    {
        printf("Failed to open /tmp/CORENAME\n");
        return;
    }

    if (fgets(core, BUFFER_SIZE - 1, file) != NULL)
    {
        if (strcmp(_core, core) != 0)
        {
            printf("Core: %s\n", core);

            if (strlen(_core) > 0)
                free(_core);

            _core = malloc(strlen(core) + 1);
            strcpy(_core, core);

            if (notify->watchroms > 0)
            {
                inotify_rm_watch(notify->id, notify->watchroms);
                notify->watchroms = 0;
            }

            char gamespath[BUFFER_SIZE];
            sprintf(gamespath, "%s/%s", GAMES_PATH, core);
            printf("ROM path: %s\n", gamespath);

            DIR *gamesdir;
            if ((gamesdir = opendir(gamespath)) != NULL) 
            {
                closedir(gamesdir);

                if ((notify->watchroms = inotify_add_watch(notify->id, gamespath, IN_OPEN)) < 0)
                {
                    printf("Failed to watch ROM path\n");
                }
            }
            else
            {
                printf("ROM path does not exist\n");
            }

            writestr(portal, "core");
            writestr(portal, _core);
            writeeom(portal);

            readrom(portal, "");
        }
    }
    else 
    {
        printf("Failed to read /tmp/CORENAME\n");
    }

    fclose(file);
}

bool portalinit(struct Portal *portal, char *portalpath)
{
    portal->fd = 0;
    portal->cmdstackpos = 0;
    portal->readbuf = malloc(BUFFER_SIZE);
    portal->readpos = 0;

    if ((portal->fd = open(portalpath, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK)) < 0) 
    {
        printf("Error opening %s: %s\n", portalpath, strerror(errno));
        return false;
    }

    if (!setinterface(portal, B115200))
    {
        printf("Failed to configure portal\n");
        return false;
    }

    return true;
}

bool portalframe(struct Portal *portal)
{
    int readlen = read(portal->fd, portal->readbuf + portal->readpos, BUFFER_SIZE - portal->readpos - 1);
    if (readlen > 0)
    {
        for (int i = 0; i < readlen; i++)
        {
            if (portal->readbuf[0] == EOM) // End of stack
            {
                printf("Running command\n");
                
                runcmd(portal);

                for (int j = 0; j < portal->cmdstackpos; j++)
                    free(portal->cmdstack[j]);

                portal->cmdstackpos = 0;

                if (readlen > i + 1)
                    memcpy(portal->readbuf, portal->readbuf + 1, readlen - 1);
            }
            else if (portal->readbuf[portal->readpos] == 0) // End of item
            {
                char *cmd = malloc(portal->readpos + 1);
                memcpy(cmd, portal->readbuf, portal->readpos + 1);
                portal->cmdstack[portal->cmdstackpos++] = cmd;

                printf("Read %d: \"%s\"\n", portal->readpos, cmd);

                if (readlen > i + 1)
                    memcpy(portal->readbuf, portal->readbuf + portal->readpos + 1, portal->readpos + (readlen - i - 1));
                
                portal->readpos = 0;
            }
            else
            {
                portal->readpos++;
            }
        }
    }
    else if (readlen == -1)
    {
        // No data available
    }
    else if (readlen < 0)
    {
        printf("Error from read: %d: %s\n", readlen, strerror(errno));
        return false;
    }
    else // readlen == 0
    {  
        printf("Timeout from read\n");
        return false;
    }

    return true;
}

void portalclose(struct Portal *portal)
{
    for (int j = 0; j < portal->cmdstackpos; j++)
        free(portal->cmdstack[j]);

    free(portal->readbuf);

    if (portal->fd > 0)
        close(portal->fd);
}

bool notifyinit(struct Notify *notify, struct Portal *portal)
{
    notify->id = 0;
    notify->readbuf = malloc(EVENT_BUFFER_SIZE);
    notify->watchcore = 0;
    notify->watchroms = 0;
    notify->portal = portal;

    if ((notify->id = inotify_init()) < 0)
    {
        printf("Failed to initialize inotify");
        return false;
    }

    int flags = fcntl(notify->id, F_GETFL, 0);
    fcntl(notify->id, F_SETFL, flags | O_NONBLOCK);

    if ((notify->watchcore = inotify_add_watch(notify->id, "/tmp/CORENAME", IN_MODIFY)) < 0)
    {
        printf("Failed to watch /tmp/CORENAME");
        return false;
    }

    return true;
}

bool notifyframe(struct Notify *notify)
{
    int readlen = read(notify->id, notify->readbuf, EVENT_BUFFER_SIZE);
    if (readlen > 0)
    {
        struct inotify_event *event;
        for (int i = 0; i < readlen; i += EVENT_SIZE + event->len)
        {
            event = (struct inotify_event *) &notify->readbuf[i];

            if (event->wd == notify->watchcore)
            {
                printf("Core changed\n");
                readcore(notify->portal, notify);
            }
            else if (notify->watchcore > 0 && event->wd == notify->watchroms)
            {
                if (event->len > 0)
                {
                    printf("Game opened\n");
                    printf("%s\n", event->name);
                    readrom(notify->portal, event->name);
                }
            }
        }
    }
    else if (readlen == -1)
    {
        // No data available
    }
    else if (readlen < 0)
    {
        printf("Error from notify read: %d: %s\n", readlen, strerror(errno));
        return false;
    }
    else // readlen == 0
    {  
        printf("Timeout from notify read\n");
        return false;
    }

    return true;
}

void notifyclose(struct Notify *notify)
{
    free(notify->readbuf);

    if (notify->id > 0)
    {
        if (notify->watchcore > 0)
            inotify_rm_watch(notify->id, notify->watchcore);

        if (notify->watchroms > 0)
            inotify_rm_watch(notify->id, notify->watchroms);

        close(notify->id);
    }
}

void process(char *portalpath)
{
    struct Portal portal;
    if (portalinit(&portal, portalpath))
    {
        struct Notify notify;
        if (notifyinit(&notify, &portal))
        {
            // Force read of core
            _core = "";
            readcore(&portal, &notify);

            while (!_terminated)
            {
                if (!portalframe(&portal))
                    break;

                if (!notifyframe(&notify))
                    break;
                
                msleep(100);
            }

            notifyclose(&notify);
        }

        portalclose(&portal);
    }
}

void signalhandler(int signal)
{
	switch (signal)
    {
		case SIGTERM:
		case SIGABRT:
		case SIGQUIT:
		case SIGINT:
			shutdown();
			break;
		case SIGHUP:
			// TODO: What to do in this case?
			break;
		case SIGSEGV:
		case SIGILL:
			// Display back trace.
			exit(EXIT_FAILURE);
			break;
	}
}

void setupsignals()
{
	signal(SIGTERM, signalhandler);
	signal(SIGQUIT, signalhandler);
	signal(SIGABRT, signalhandler);
	signal(SIGINT,  signalhandler);
	signal(SIGCONT, SIG_IGN);
	signal(SIGSTOP, SIG_IGN);
	signal(SIGHUP,  signalhandler);	
}

bool initialize()
{
    int rc;

	_terminated = false;
    _core = "";
    _rom = "";

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

    char mbcpath[BUFFER_SIZE];
    sprintf(mbcpath, "%s/mbc", selfpath);
    _mbcpath = malloc(strlen(mbcpath) + 1);
    strcpy(_mbcpath, mbcpath);

    printf("Batch application path: %s\n", _mbcpath);

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

void cleanup()
{
    mdb_dbi_close(_db.env, _db.dbi);
    mdb_env_close(_db.env);
}

int main(int argc, char *argv[])
{
    char *portalpath = (argc > 1) ? argv[1] : "/dev/ttyACM2";

	if (!initialize())
		return 1;

    if (dbtxnopen(false))
    {
        dbput("TESTING", "ONE");
        dbput("TESTING", "TWO");
        dbput("TESTING", "THREE");
        dbput("TESTING", "FOUR");
        dbput("TESTING", "FIVE");
        dbput("TESTING", "SIX");
        dbdel("TESTING", "THREE");

        dbtxnclose();
    }

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
                }
                while (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_NEXT)));
            }

            dbcurclose();
        }

        dbtxnclose();
    }

	setupsignals();

	printf("Entering main loop...\n");
	
	while (!_terminated)
	{
		process(portalpath);

        if (!_terminated)
        {
            printf("Connection failed. Attempting to reconnect in 10 seconds.\n");

            for (int i = 0; i < 100 && !_terminated; i++)
            {
                msleep(100);
            }
        }
	}

	printf("\n");
	printf("Cleaning up!\n");

	cleanup();

	printf("All done!\n");

	return 0;
}