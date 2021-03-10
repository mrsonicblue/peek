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
#include <time.h>
#include <db.h>
#include <path.h>

#define GAMES_PATH "/media/fat/games"
#define MOUNT_NAME "Peek"
#define MAX_RECENTS 20
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
    int retry;
};

struct Notify
{
    int id;
    int watchcore;
    int watchroms;
    char *readbuf;
    struct Portal *portal;
    int retry;
};

static volatile int _terminated;
static char *_portalpath;
static char *_mbcpath;
static char *_core;
static char *_rom;
static char *_peekmountpath;
static struct Database _db;

void shutdown()
{
	if (_terminated)
		return;

	_terminated = 1;
}

int setinterface(int fd, int speed)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0)
    {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    cfmakeraw(&tty);

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

void nowchar(char *str)
{
    time_t now = time(NULL);
    now = now & 0xFFFFFFFF; // Normalize to 4 bytes
    now = 0xFFFFFFFF - now; // Flip to count down
    char *nowptr = (char *)&now;
    int offset;
    for (int i = 0; i < TIME_LEN; i += 2)
    {
        offset = sizeof(time_t) - (i / 2) - 1;
        str[i] = 'a' + (nowptr[offset] >> 4);
        str[i + 1] = 'a' + (nowptr[offset] & 0x0F);
    }
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

void updaterecents()
{
    if (strlen(_core) == 0 || strlen(_rom) == 0)
        return;

    char key[BUFFER_SIZE];
    sprintf(key, "rec/%s", _core);

    if (!dbtxnopen(&_db, 0))
    {
        int rc;
        if (!dbcuropen(&_db))
        {
            MDB_val dbkey = {strlen(key) + 1, key};
            MDB_val dbdata;
            if (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET)))
            {
                int count = 1;
                do
                {
                    if (count >= MAX_RECENTS || dbdata.mv_size < TIME_LEN + 1 || strcmp(_rom, dbdata.mv_data + TIME_LEN) == 0)
                    {
                        mdb_cursor_del(_db.cur, 0);
                    }
                    else
                    {
                        count++;
                    }
                }
                while (!(rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_NEXT_DUP)));
            }

            char *stamped = malloc(strlen(_rom) + 1 + TIME_LEN);
            nowchar(stamped);
            strcpy(stamped + TIME_LEN, _rom);
            dbput(&_db, key, stamped);
            free(stamped);

            dbcurclose(&_db);
        }

        dbtxnclose(&_db);
    }
}

int writeraw(struct Portal *portal, char *s, int sendlen)
{
    if (!portal->fd)
        return -1;
    
    int writelen = write(portal->fd, s, sendlen);
    if (writelen != sendlen)
    {
        printf("Error from write: %d, %d\n", writelen, errno);
    }
    tcdrain(portal->fd); // wait for write

    return 0;
}

void writestr(struct Portal *portal, char *s)
{
    int sendlen = strlen(s) + 1;
    if (!writeraw(portal, s, sendlen))
        printf("Sent: %s\n", s);
}

void writeeol(struct Portal *portal)
{
    char s[] = {0x00};
    if (!writeraw(portal, s, 1))
        printf("Sent EOL\n");
}

void writeeom(struct Portal *portal)
{
    char s[] = {EOM};    
    if (!writeraw(portal, s, 1))
        printf("Sent EOM\n");
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
            if (!writeraw(portal, procbuf, len))
                printf("Sending %d bytes\n", len);
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

void peekunmount()
{
    if (!_peekmountpath)
        return;

    printf("Unmounting: %s\n", _peekmountpath);

    free(_peekmountpath);
    _peekmountpath = (char *)NULL;
}

void peekmount(char *romspath)
{
    peekunmount();

    _peekmountpath = malloc(strlen(romspath) + strlen(MOUNT_NAME) + 2);
    sprintf(_peekmountpath, "%s/%s", romspath, MOUNT_NAME);

    printf("Mounting: %s\n", _peekmountpath);
}

void readrom(struct Portal *portal, char *rom)
{
    if (strcmp(_rom, rom) == 0)
        return;
    
    printf("ROM switched to: %s\n", rom);
    
    if (strlen(_rom) > 0)
        free(_rom);

    _rom = malloc(strlen(rom) + 1);
    strcpy(_rom, rom);

    updaterecents();

    writestr(portal, "rom");
    writestr(portal, _rom);
    writeeom(portal);
}

void readcore(struct Notify *notify)
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
            printf("Core switched to: %s\n", core);

            if (strlen(_core) > 0)
                free(_core);
            
            peekunmount();

            _core = malloc(strlen(core) + 1);
            strcpy(_core, core);

            if (notify->watchroms)
            {
                inotify_rm_watch(notify->id, notify->watchroms);
                notify->watchroms = 0;
            }

            char romspath[BUFFER_SIZE];
            sprintf(romspath, "%s/%s", GAMES_PATH, core);
            printf("ROMs path: %s\n", romspath);

            DIR *romsdir;
            if ((romsdir = opendir(romspath)) != NULL) 
            {
                closedir(romsdir);

                int watchroms;
                if ((watchroms = inotify_add_watch(notify->id, romspath, IN_OPEN)) < 0)
                {
                    printf("Failed to watch ROM path: %s\n", romspath);
                }
                else
                {
                    notify->watchroms = watchroms;
                }

                peekmount(romspath);
            }
            else
            {
                printf("ROM path does not exist\n");
            }

            writestr(notify->portal, "core");
            writestr(notify->portal, _core);
            writeeom(notify->portal);

            readrom(notify->portal, "");
        }
    }
    else 
    {
        printf("Failed to read /tmp/CORENAME\n");
    }

    fclose(file);
}

void portalinit(struct Portal *portal)
{
    portal->fd = 0;
    portal->cmdstackpos = 0;
    portal->readbuf = NULL;
    portal->readpos = 0;
    portal->retry = 0;
}

int portalopen(struct Portal *portal)
{
    int fd;
    if ((fd = open(_portalpath, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK)) < 0) 
    {
        printf("Error opening %s: %s\n", _portalpath, strerror(errno));
        return -1;
    }

    if (setinterface(fd, B115200))
    {
        printf("Failed to configure portal\n");
        close(fd);
        return -1;
    }

    portal->fd = fd;
    portal->readbuf = malloc(BUFFER_SIZE);

    return 0;
}

void portalclose(struct Portal *portal)
{
    for (int j = 0; j < portal->cmdstackpos; j++)
        free(portal->cmdstack[j]);

    if (portal->readbuf)
    {
        free(portal->readbuf);
        portal->readbuf = NULL;        
    }

    if (portal->fd)
    {
        close(portal->fd);
        portal->fd = 0;
    }

    portal->retry = 100;
}

void portalframe(struct Portal *portal)
{
    if (!portal->fd)
    {
        if (portal->retry > 0)
        {
            portal->retry--;
            return;
        }
        else
        {
            if (portalopen(portal))
            {
                printf("Failed to open portal connection. Attempting to reconnect in 10 seconds.\n");
                portalclose(portal);
                return;
            }
            else
            {
                // Force send core
                writestr(portal, "core");
                writestr(portal, _core);
                writeeom(portal);

                // Force send rom
                writestr(portal, "rom");
                writestr(portal, _rom);
                writeeom(portal);
            }
        }
    }

    int res;
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

        res = 0;
    }
    else if (readlen == -1)
    {
        // No data available
        res = 0;
    }
    else if (readlen < 0)
    {
        printf("Error from read: %d: %s\n", readlen, strerror(errno));
        res = -1;
    }
    else // readlen == 0
    {  
        printf("Timeout from read\n");
        res = -1;
    }

    if (res != 0)
    {
        printf("Portal connection failed. Attempting to reconnect in 10 seconds.\n");
        portalclose(portal);
    }
}

void notifyinit(struct Notify *notify, struct Portal *portal)
{
    notify->id = 0;
    notify->readbuf = NULL;
    notify->watchcore = 0;
    notify->watchroms = 0;
    notify->portal = portal;
    notify->retry = 0;
}

int notifyopen(struct Notify *notify)
{
    int id;
    if ((id = inotify_init()) < 0)
    {
        printf("Failed to initialize inotify");
        return -1;
    }

    int flags = fcntl(id, F_GETFL, 0);
    fcntl(id, F_SETFL, flags | O_NONBLOCK);

    int watchcore;
    if ((watchcore = inotify_add_watch(id, "/tmp/CORENAME", IN_MODIFY)) < 0)
    {
        printf("Failed to watch /tmp/CORENAME\n");
        close(id);
        return -1;
    }

    notify->id = id;
    notify->readbuf = malloc(EVENT_BUFFER_SIZE);
    notify->watchcore = watchcore;

    return 0;
}

void notifyclose(struct Notify *notify)
{
    if (notify->readbuf)
    {
        free(notify->readbuf);
        notify->readbuf = NULL;
    }

    if (notify->id)
    {
        if (notify->watchcore)
            inotify_rm_watch(notify->id, notify->watchcore);

        if (notify->watchroms)
            inotify_rm_watch(notify->id, notify->watchroms);

        close(notify->id);
        notify->id = 0;
    }

    notify->retry = 100;
}

void notifyframe(struct Notify *notify)
{
    if (!notify->id)
    {
        if (notify->retry > 0)
        {
            notify->retry--;
            return;
        }
        else
        {
            if (notifyopen(notify))
            {
                printf("Failed to initialize notify. Attempting to reinitialize in 10 seconds.\n");
                notifyclose(notify);
                return;
            }
            else
            {
                readcore(notify);
            }
        }
    }

    int res;
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
                readcore(notify);
            }
            else if (notify->watchroms && event->wd == notify->watchroms)
            {
                if (event->len > 0)
                {
                    printf("Game opened: %s\n", event->name);
                    readrom(notify->portal, event->name);
                }
            }
        }

        res = 0;
    }
    else if (readlen == -1)
    {
        // No data available
        res = 0;
    }
    else if (readlen < 0)
    {
        printf("Error from notify read: %d: %s\n", readlen, strerror(errno));
        res = -1;
    }
    else // readlen == 0
    {  
        printf("Timeout from notify read\n");
        res = -1;
    }

    if (res != 0)
    {
        printf("Notify failed. Attempting to reinitialize in 10 seconds.\n");
        notifyclose(notify);
    }
}

void process()
{
    struct Portal portal;
    portalinit(&portal);

    struct Notify notify;
    notifyinit(&notify, &portal);

    while (!_terminated)
    {
        portalframe(&portal);
        notifyframe(&notify);
        msleep(100);
    }

    notifyclose(&notify);
    portalclose(&portal);
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
    _peekmountpath = (char *)NULL;

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
    peekunmount();
    dbclose(&_db);
}

int main_service(int argc, char *argv[])
{
    printf("Starting service...\n");

    _portalpath = (argc > 2) ? argv[2] : "/dev/ttyACM2";

	if (initialize())
		return 1;

	setupsignals();

	printf("Entering main loop...\n");
	
    process();

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
                rc = mdb_cursor_get(_db.cur, &dbkey, &dbdata, MDB_SET);
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

int main_db_pre(int argc, char *argv[], int delete)
{
    if (argc < 4)
    {
        printf("Get prefix command requires prefix\n");
        return 1;
    }

    char *prefix = argv[3];
    size_t prefixlen = strlen(prefix);

    if (!dbtxnopen(&_db, delete ? 0 : 1))
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

                    if (delete)
                        mdb_cursor_del(_db.cur, 0);
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
        printf("Get slice command requires prefix\n");
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

void main_db_import_put(char *core, char *rom, char *has, char *value)
{
    if (strlen(value) == 0)
        return;

    char key[BUFFER_SIZE];

    char *r = NULL;
    char *bit;
    for (bit = strtokplus(value, '|', &r); bit != NULL; bit = strtokplus(NULL, '|', &r))
    {
        if (strlen(bit) == 0)
            continue;

        printf(" - %s: %s\n", has, bit);

        sprintf(key, "has/%s/%s/%s", core, has, bit);
        dbput(&_db, key, rom);
    }
}

int main_db_import(int argc, char *argv[])
{
    if (argc < 5)
    {
        printf("Import command requires core name and filename\n");
        return 1;
    }

    char *core = argv[3];
    char *filename = argv[4];

    printf("Importing data for core: %s\n", core);
    printf("Opening %s\n", filename);

    FILE *file;
    char line[BUFFER_SIZE];
    char rom[BUFFER_SIZE];

    if ((file = fopen(filename, "r")) == NULL) 
    {
        printf("Failed to open file\n");
        return 1;
    }

    if (!dbtxnopen(&_db, 0))
    {
        char *r = NULL;
        char *bit;
        char *tmp;

        char *header[100];
        int headerlen = 0;

        if (fgets(line, BUFFER_SIZE - 1, file) != NULL)
        {
            rtrim(line);

            for (bit = strtokplus(line, '\t', &r); bit != NULL; bit = strtokplus(NULL, '\t', &r))
            {
                tmp = malloc(strlen(bit) + 1);
                strcpy(tmp, bit);
                
                header[headerlen++] = tmp;

                printf("Header: %s\n", tmp);
            }
        }
        else
        {
            printf("Failed to read header\n");
            return 1;
        }

        while (fgets(line, BUFFER_SIZE - 1, file) != NULL)
        {
            rtrim(line);

            int pos = 0;
            for (bit = strtokplus(line, '\t', &r); bit != NULL; bit = strtokplus(NULL, '\t', &r))
            {
                if (pos == 0)
                {
                    // We assume the first colume is the ROM
                    strcpy(rom, bit);
                    printf("%s\n", rom);
                }
                else
                {
                    main_db_import_put(core, rom, header[pos], bit);
                }

                pos++;
            }
        }

        for (int i = 0; i < headerlen; i++)
        {
            free(header[i]);
        }

        dbtxnclose(&_db);
    }

    fclose(file);

    // printf("Putting: %s --- %s\n", key, value);

    // if (!dbtxnopen(&_db, 0))
    // {
    //     dbput(&_db, key, value);
    //     dbtxnclose(&_db);
    // }

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
    else if (strcmp(cmd, "getpre") == 0)
    {
        res = main_db_pre(argc, argv, 0);
    }
    else if (strcmp(cmd, "getsli") == 0)
    {
        res = main_db_sli(argc, argv);
    }
    else if (strcmp(cmd, "put") == 0)
    {
        res = main_db_put(argc, argv);
    }
    else if (strcmp(cmd, "del") == 0)
    {
        res = main_db_del(argc, argv);
    }
    else if (strcmp(cmd, "delpre") == 0)
    {
        res = main_db_pre(argc, argv, 1);
    }
    else if (strcmp(cmd, "import") == 0)
    {
        res = main_db_import(argc, argv);
    }
    else
    {
        printf("Unknown database command: %s\n", cmd);
        res = 1;
    }

    dbclose(&_db);

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