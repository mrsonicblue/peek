#include <stdio.h>
#include <stdlib.h>
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
#include <db.h>
#include <path.h>

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

struct Notify
{
    int id;
    int watchcore;
    int watchroms;
    char *readbuf;
    struct Portal *portal;
};

static volatile int _terminated;
static char *_mbcpath;
static char *_core;
static char *_rom;
static struct Database _db;

void shutdown()
{
	if (_terminated)
		return;

	_terminated = 1;
}

int setinterface(struct Portal *portal, int speed)
{
    struct termios tty;

    if (tcgetattr(portal->fd, &tty) < 0)
    {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    cfmakeraw(&tty);

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(portal->fd, TCSANOW, &tty) != 0)
    {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }

    return 0;
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
    if (!dbtxnopen(&_db, 1))
    {
        int rc;

        if (!dbcuropen(&_db))
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

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }
}

void runcmd_dbchk(struct Portal *portal, char *cmdkey, char *key, char *data)
{
    if (!dbtxnopen(&_db, 1))
    {
        int rc;

        if (!dbcuropen(&_db))
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

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }
}

void runcmd_dbput(struct Portal *portal, char *cmdkey, char *key, char *value)
{
    if (!dbtxnopen(&_db, 0))
    {
        dbput(&_db, key, value);

        dbtxnclose(&_db);
    }
}

void runcmd_dbdel(struct Portal *portal, char *cmdkey, char *key, char *value)
{
    if (!dbtxnopen(&_db, 0))
    {
        dbdel(&_db, key, value);

        dbtxnclose(&_db);
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

int portalinit(struct Portal *portal, char *portalpath)
{
    portal->fd = 0;
    portal->cmdstackpos = 0;
    portal->readbuf = malloc(BUFFER_SIZE);
    portal->readpos = 0;

    if ((portal->fd = open(portalpath, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK)) < 0) 
    {
        printf("Error opening %s: %s\n", portalpath, strerror(errno));
        return -1;
    }

    if (setinterface(portal, B115200))
    {
        printf("Failed to configure portal\n");
        return -1;
    }

    return 0;
}

int portalframe(struct Portal *portal)
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
        return -1;
    }
    else // readlen == 0
    {  
        printf("Timeout from read\n");
        return -1;
    }

    return 0;
}

void portalclose(struct Portal *portal)
{
    for (int j = 0; j < portal->cmdstackpos; j++)
        free(portal->cmdstack[j]);

    free(portal->readbuf);

    if (portal->fd > 0)
        close(portal->fd);
}

int notifyinit(struct Notify *notify, struct Portal *portal)
{
    notify->id = 0;
    notify->readbuf = malloc(EVENT_BUFFER_SIZE);
    notify->watchcore = 0;
    notify->watchroms = 0;
    notify->portal = portal;

    if ((notify->id = inotify_init()) < 0)
    {
        printf("Failed to initialize inotify");
        return -1;
    }

    int flags = fcntl(notify->id, F_GETFL, 0);
    fcntl(notify->id, F_SETFL, flags | O_NONBLOCK);

    if ((notify->watchcore = inotify_add_watch(notify->id, "/tmp/CORENAME", IN_MODIFY)) < 0)
    {
        printf("Failed to watch /tmp/CORENAME");
        return -1;
    }

    return 0;
}

int notifyframe(struct Notify *notify)
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
        return -1;
    }
    else // readlen == 0
    {  
        printf("Timeout from notify read\n");
        return -1;
    }

    return 0;
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
    if (!portalinit(&portal, portalpath))
    {
        struct Notify notify;
        if (!notifyinit(&notify, &portal))
        {
            // Force read of core
            _core = "";
            readcore(&portal, &notify);

            // Force send rom
            writestr(&portal, "rom");
            writestr(&portal, _rom);
            writeeom(&portal);

            while (!_terminated)
            {
                if (portalframe(&portal))
                    break;

                if (notifyframe(&notify))
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

int initialize()
{
	_terminated = 0;
    _core = "";
    _rom = "";

    if (dbopen(&_db))
        return -1;

    // char mbcpath[BUFFER_SIZE];
    // sprintf(mbcpath, "%s/mbc", selfpath);
    // _mbcpath = malloc(strlen(mbcpath) + 1);
    // strcpy(_mbcpath, mbcpath);

    return 0;
}

void cleanup()
{
    mdb_dbi_close(_db.env, _db.dbi);
    mdb_env_close(_db.env);
}

int main_service(int argc, char *argv[])
{
    printf("Starting service...\n");

    char *portalpath = (argc > 2) ? argv[2] : "/dev/ttyACM2";

	if (initialize())
		return 1;

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

int main_db_get(int argc, char *argv[])
{
    char *key = (argc > 3) ? argv[3] : (char *)NULL;

    if (!dbtxnopen(&_db, 1))
    {
        int rc;
        if (!dbcuropen(&_db))
        {
            MDB_val dbkey;
            MDB_val dbdata;
            MDB_cursor_op op;

            if (key)
            {
                dbkey.mv_size = strlen(key) + 1;
                dbkey.mv_data = key;
                rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET_RANGE);
                op = MDB_NEXT_DUP;
            }
            else
            {
                rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_FIRST);
                op = MDB_NEXT;
            }

            if (rc)
            {
                printf("No data found: %d\n", rc);
            }
            else
            {
                printf("Data found!\n");
                do
                {
                    printf("Data: %s --- %s\n", (char *)dbkey.mv_data, (char *)dbdata.mv_data);
                }
                while (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, op)));
            }

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }

    return 0;
}

int main_db_put(int argc, char *argv[])
{
    if (argc < 5)
    {
        printf("Put command requires key and value\n");
        return 1;
    }

    char *key = argv[3];
    char *value = argv[4];

    printf("Putting: %s --- %s\n", key, value);

    if (!dbtxnopen(&_db, 0))
    {
        dbput(&_db, key, value);
        dbtxnclose(&_db);
    }

    return 0;
}

int main_db_del(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("Delete command requires key\n");
        return 1;
    }

    char *key = argv[3];
    char *value = (argc > 4) ? argv[4] : (char *)NULL;

    printf("Deleting: %s --- %s\n", key, value);

    if (!dbtxnopen(&_db, 0))
    {
        dbdel(&_db, key, value);
        dbtxnclose(&_db);
    }

    return 0;
}

int main_db_pre(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("Prefix command requires prefix\n");
        return 1;
    }

    char *prefix = argv[3];
    size_t prefixlen = strlen(prefix);

    if (!dbtxnopen(&_db, 1))
    {
        int rc;
        if (!dbcuropen(&_db))
        {
            MDB_val dbkey = {prefixlen + 1, prefix};
            MDB_val dbdata;

            rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET_RANGE);
            if (rc)
            {
                printf("No data found: %d\n", rc);
            }
            else
            {
                printf("Data found!\n");
                do
                {
                    if (prefixlen > dbkey.mv_size || memcmp(prefix, dbkey.mv_data, prefixlen) != 0)
                        break;

                    printf("Data: %s --- %s\n", (char *)dbkey.mv_data, (char *)dbdata.mv_data);
                }
                while (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_NEXT)));
            }

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }

    return 0;
}

int main_db_sli(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("Slice command requires prefix\n");
        return 1;
    }

    char *prefix = argv[3];
    size_t prefixlen = strlen(prefix);
    char slice[BUFFER_SIZE];
    size_t slicelen = 0;

    if (!dbtxnopen(&_db, 1))
    {
        int rc;
        if (!dbcuropen(&_db))
        {
            MDB_val dbkey = {prefixlen + 1, prefix};
            MDB_val dbdata;

            rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET_RANGE);
            if (rc)
            {
                printf("No data found: %d\n", rc);
            }
            else
            {
                printf("Data found!\n");
                do
                {
                    if (prefixlen > dbkey.mv_size || memcmp(prefix, dbkey.mv_data, prefixlen) != 0)
                        break;
                    
                    char *curstart = (char *)dbkey.mv_data + prefixlen;
                    char *curend = strchr(curstart, '/');
                    size_t curlen = curend ? (size_t)(curend - curstart) : (dbkey.mv_size - 1 - prefixlen);

                    if (slicelen != curlen || memcmp(slice, curstart, slicelen) != 0)
                    {
                        memcpy(slice, curstart, curlen);
                        slice[curlen] = '\0';
                        slicelen = curlen;

                        printf("%s\n", slice);
                    }
                }
                while (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_NEXT)));
            }

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }

    return 0;
}

int main_db(int argc, char *argv[])
{
    printf("Running database command...\n");

    if (dbopen(&_db))
        return -1;
    
    char *cmd = (argc > 2) ? argv[2] : "get";

    int res;
    if (strcmp(cmd, "get") == 0)
    {
        res = main_db_get(argc, argv);
    }
    else if (strcmp(cmd, "put") == 0)
    {
        res = main_db_put(argc, argv);
    }
    else if (strcmp(cmd, "del") == 0)
    {
        res = main_db_del(argc, argv);
    }
    else if (strcmp(cmd, "pre") == 0)
    {
        res = main_db_pre(argc, argv);
    }
    else if (strcmp(cmd, "sli") == 0)
    {
        res = main_db_sli(argc, argv);
    }
    else
    {
        printf("Unknown database command: %s\n", cmd);
        res = 1;
    }

    mdb_dbi_close(_db.env, _db.dbi);
    mdb_env_close(_db.env);

    return res;
}

int main(int argc, char *argv[])
{
    char *cmd = (argc > 1) ? argv[1] : "service";

    if (strcmp(cmd, "service") == 0)
    {
        return main_service(argc, argv);
    }
    else if (strcmp(cmd, "db") == 0)
    {
        return main_db(argc, argv);
    }

    printf("Unknown command: %s\n", cmd);
    return 1;
}