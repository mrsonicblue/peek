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
#include <termios.h>

#define PORTAL "/dev/ttyACM2"
#define BUFFER_SIZE 4096
#define EOM 4
#define EVENT_SIZE ( sizeof (struct inotify_event) )
#define EVENT_BUFFER_SIZE ( 4 * ( EVENT_SIZE + 16 ) )

static volatile bool _terminated;
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
	_terminated = false;
    corecur[0] = 0;
    romcur[0] = 0;

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
    inotify_rm_watch(inote, inotecore);
    if (inoteroms > 0)
        inotify_rm_watch(inote, inoteroms);
    close(inote);
}

int main(int argc, char *argv[])
{
	if (!initialize())
		return 1;

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