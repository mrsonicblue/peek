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

#define PORTAL "/dev/ttyACM2"
#define BUFFER_SIZE 4096
#define EOM 4
#define EVENT_SIZE ( sizeof (struct inotify_event) )
#define EVENT_BUFFER_SIZE ( 4 * ( EVENT_SIZE + 16 ) )

static volatile bool _terminated;
char *_mbcpath;
MDB_env *_dbenv;
MDB_txn *_dbtxn;
MDB_dbi _dbi;
int inote;
int inotecore;
int inoteroms;
char inotebuf[EVENT_BUFFER_SIZE];
char corecur[BUFFER_SIZE];
char romcur[BUFFER_SIZE];

void shutdown()
{
	if (_terminated)
		return;

	_terminated = true;
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

void writeraw(int portal, char *s, int sendlen)
{
    int writelen = write(portal, s, sendlen);
    if (writelen != sendlen)
    {
        printf("Error from write: %d, %d\n", writelen, errno);
    }
    tcdrain(portal); // wait for write
}

void writestr(int portal, char *s)
{
    int sendlen = strlen(s) + 1;
    writeraw(portal, s, sendlen);
}

void writeeol(int portal)
{
    char s[] = {0x00};
    printf("Sending EOL\n");
    writeraw(portal, s, 1);
}

void writeeom(int portal)
{
    char s[] = {EOM};    
    printf("Sending EOM\n");
    writeraw(portal, s, 1);
}

void runcmdproc(int portal, char *key, char *path)
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
        writestr(portal, key);

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

void runcmdbproc(int portal, char *key, char *path)
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

void runcmd(int portal, char **cmds, int cmdcnt)
{
    if (cmdcnt < 2)
    {
        printf("All commands requires at least two arguments\n");
        return;
    }
    
    if (strcmp(cmds[1], "proc") == 0)
    {
        // Run process
        if (cmdcnt < 3)
        {
            printf("Process command requires three arguments\n");
            return;
        }

        runcmdproc(portal, cmds[0], cmds[2]);
    }
    else if (strcmp(cmds[1], "bproc") == 0)
    {
        // Run background process
        if (cmdcnt < 3)
        {
            printf("Background process command requires three arguments\n");
            return;
        }

        runcmdbproc(portal, cmds[0], cmds[2]);
    }
    else
    {
        printf("Unknown command type: %s\n", cmds[1]);
    }
}

void readrom(int portal, char *rom)
{
    if (strcmp(romcur, rom) == 0)
        return;

    strcpy(romcur, rom);

    writestr(portal, "rom");
    writestr(portal, romcur);
    writeeom(portal);
}

void readcore(int portal)
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
        if (strcmp(core, corecur) != 0)
        {
            printf("Core: %s\n", core);

            strcpy(corecur, core);

            if (inoteroms > 0)
            {
                inotify_rm_watch(inote, inoteroms);
                inoteroms = 0;
            }

            char gamespath[BUFFER_SIZE];
            sprintf(gamespath, "/media/fat/games/%s", core);
            printf("ROM path: %s\n", gamespath);

            DIR *gamesdir;
            if ((gamesdir = opendir(gamespath)) != NULL) 
            {
                closedir(gamesdir);

                if ((inoteroms = inotify_add_watch(inote, gamespath, IN_OPEN)) < 0)
                {
                    printf("Failed to watch ROM path\n");
                }
            }
            else
            {
                printf("ROM path does not exist\n");
            }

            writestr(portal, "core");
            writestr(portal, corecur);
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

void checkinote(int portal)
{
    int readlen = read(inote, inotebuf, EVENT_BUFFER_SIZE);

    if (readlen > 0)
    {
        struct inotify_event *event;
        for (int i = 0; i < readlen; i += EVENT_SIZE + event->len)
        {
            event = (struct inotify_event *) &inotebuf[i];

            if (event->wd == inotecore)
            {
                printf("Core changed\n");
                readcore(portal);
            }
            else if (inoteroms > 0 && event->wd == inoteroms) 
            {
                if (event->len > 0)
                {
                    printf("Game opened\n");
                    printf("%s\n", event->name);
                    readrom(portal, event->name);
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
        printf("Error from read: %d: %s\n", readlen, strerror(errno));
    }
    else // readlen == 0
    {  
        printf("Timeout from read\n");
    }
}

void process()
{
    char *cmds[20];
    int cmdspos = 0;
    char readbuf[BUFFER_SIZE];
    int readpos = 0;

    int portal = open(PORTAL, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
    if (portal < 0) 
    {
        printf("Error opening %s: %s\n", PORTAL, strerror(errno));
        return;
    }

    if (setinterface(portal, B115200) != 0)
        return;

    // Force read of core
    corecur[0] = 0;
    readcore(portal);

    while (!_terminated)
    {
        int readlen = read(portal, readbuf + readpos, BUFFER_SIZE - readpos - 1);
        if (readlen > 0)
        {
            for (int i = 0; i < readlen; i++)
            {
                if (readbuf[0] == EOM) // End of stack
                {
                    printf("Running command\n");
                    
                    runcmd(portal, cmds, cmdspos);

                    for (int j = 0; j < cmdspos; j++)
                        free(cmds[j]);
                    
                    cmdspos = 0;

                    if (readlen > i + 1)
                        memcpy(readbuf, readbuf + 1, readlen - 1);
                }
                else if (readbuf[readpos] == 0) // End of item
                {
                    char *cmd = malloc(readpos + 1);
                    memcpy(cmd, readbuf, readpos + 1);
                    cmds[cmdspos++] = cmd;

                    printf("Read %d: \"%s\"\n", readpos, cmd);

                    if (readlen > i + 1)
                        memcpy(readbuf, readbuf + readpos + 1, readpos + (readlen - i - 1));
                    
                    readpos = 0;
                }
                else
                {
                    readpos++;
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
        }
        else // readlen == 0
        {  
            printf("Timeout from read\n");
        }

        checkinote(portal);

        msleep(100);
    }

    for (int j = 0; j < cmdspos; j++)
        free(cmds[j]);

    close(portal);
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
    corecur[0] = 0;
    romcur[0] = 0;

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
        if ((rc = mkdir(dbpath, 0700)))
        {
            printf("Failed to create database directory\n");
            return false;
        }
    }

    if ((rc = mdb_env_create(&_dbenv)))
    {
        printf("Failed to create database environment: %d\n", rc);
        return false;
    }
    //Limit large enough to accommodate all our named dbs. This only starts to matter if the number gets large, otherwise it's just a bunch of extra entries in the main table.
    mdb_env_set_maxdbs(_dbenv, 50);
    //This is the maximum size of the db (but will not be used directly), so we make it large enough that we hopefully never run into the limit.
    mdb_env_set_mapsize(_dbenv, (size_t)1048576 * (size_t)100000); // 1MB * 100000
    if ((rc = mdb_env_open(_dbenv, dbpath, MDB_NOTLS, 0664)))
    {
        printf("Failed to open database environment: %d\n", rc);
        return false;
    }

    if ((rc = mdb_txn_begin(_dbenv, NULL, 0, &_dbtxn))) 
    {
        printf("Failed to create transaction: %d\n", rc);
        return false;
    }

    if ((rc = mdb_dbi_open(_dbtxn, "test", MDB_DUPSORT | MDB_CREATE, &_dbi)))
    {
        printf("Failed to open database: %d\n", rc);
        return false;
    }

    if ((inote = inotify_init()) < 0)
    {
        printf("Failed to initialize inotify");
        return false;
    }

    int flags = fcntl(inote, F_GETFL, 0);
    fcntl(inote, F_SETFL, flags | O_NONBLOCK);

    if ((inotecore = inotify_add_watch(inote, "/tmp/CORENAME", IN_MODIFY)) < 0)
    {
        printf("Failed to watch /tmp/CORENAME");
        return false;
    }

    inoteroms = 0;

	return true;
}

void cleanup()
{
    int rc;

    inotify_rm_watch(inote, inotecore);
    if (inoteroms > 0)
        inotify_rm_watch(inote, inoteroms);
    close(inote);

    if ((rc = mdb_txn_commit(_dbtxn)))
    {
        printf("Failed to commit transaction: %d\n", rc);
    }
    mdb_dbi_close(_dbenv, _dbi);
    mdb_env_close(_dbenv);
}

int main(int argc, char *argv[])
{
	if (!initialize())
		return 1;

    int rc;
    if (true)
    {
        //Initialize the key with the key we're looking for
        char *testKey = "TESTING";
        MDB_val key = {strlen(testKey) + 1, testKey};

        char *testData = "THIRD";
        MDB_val data = {strlen(testData) + 1, testData};

        if ((rc = mdb_put(_dbtxn, _dbi, &key, &data, MDB_NODUPDATA)))
        {
            // MDB_KEYEXIST IS OK
            printf("Failed to write data: %d\n", rc);
        }
    }

    if (true)
    {
        //Initialize the key with the key we're looking for
        char *testKey = "TESTING";
        MDB_val key = {strlen(testKey) + 1, testKey};

        char *testData = "ANOTHER";
        MDB_val data = {strlen(testData) + 1, testData};

        if ((rc = mdb_del(_dbtxn, _dbi, &key, &data)))
        {
            // MDB_NOTFOUND IS OK
            printf("Failed to write data: %d\n", rc);
        }
    }

    MDB_cursor *dbcur;
    if ((rc = mdb_cursor_open(_dbtxn, _dbi, &dbcur)))
    {
        printf("Failed to open cursor: %d\n", rc);
        return 1;
    }

    if (true)
    {
        //Initialize the key with the key we're looking for
        char *testKey = "TESTING";
        MDB_val key = {strlen(testKey) + 1, testKey};
        MDB_val data;

        //Position the cursor, key and data are available in key
        if ((rc = mdb_cursor_get(dbcur, &key, &data, MDB_SET_RANGE)))
        {
            printf("No data found: %d\n", rc);
        }
        else
        {
            printf("Data found!\n");
            do
            {
                printf("Data: %s\n", (char *)data.mv_data);
            }
            while (!(rc = mdb_cursor_get(dbcur, &key, &data, MDB_NEXT)));
        }

        mdb_cursor_close(dbcur);
    }

	setupsignals();

	printf("Entering main loop...\n");
	
	while (!_terminated)
	{
		process();

        if (!_terminated)
		    sleep(1);
	}

	printf("\n");
	printf("Cleaning up!\n");

	cleanup();

	printf("All done!\n");

	return 0;
}