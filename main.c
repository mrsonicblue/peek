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
#include <termios.h>

#define PORTAL "/dev/ttyACM2"
#define BUFFER_SIZE 4096
#define EOM 4

static volatile bool _terminated;

void shutDown()
{
	if (_terminated)
		return;

	_terminated = true;
}

int set_interface_attribs(int fd, int speed)
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

void writeraw(char *s, int sendlen, int portal)
{
    int writelen = write(portal, s, sendlen);
    if (writelen != sendlen)
    {
        printf("Error from write: %d, %d\n", writelen, errno);
    }
    tcdrain(portal); // wait for write
}

void writestr(char *s, int portal)
{
    int sendlen = strlen(s) + 1;
    writeraw(s, sendlen, portal);
}

void writeeol(int portal)
{
    char s[] = {0x00};
    printf("Sending EOL\n");
    writeraw(s, 1, portal);
}

void writeeom(int portal)
{
    char s[] = {EOM};    
    printf("Sending EOM\n");
    writeraw(s, 1, portal);
}

void runcmdproc(char *key, char *path, int portal)
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
        writestr(key, portal);

        while (fgets(procbuf, BUFFER_SIZE, proc) != NULL)
        {
            // rtrim(procbuf);
            int len = strlen(procbuf);
            writeraw(procbuf, len, portal);
        }

        writeeol(portal);
        writeeom(portal);

        if (pclose(proc))
        {
            printf("Process exited with error status\n");
        }
    }
}

void runcmd(char **cmds, int cmdcnt, int portal)
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

        runcmdproc(cmds[0], cmds[2], portal);
    }
    else
    {
        printf("Unknown command type: %s\n", cmds[1]);
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

    if (set_interface_attribs(portal, B115200) != 0)
        return;

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
                    
                    runcmd(cmds, cmdspos, portal);

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
        else // readLen == 0
        {  
            printf("Timeout from read\n");
        }

        msleep(100);
    }

    for (int j = 0; j < cmdspos; j++)
        free(cmds[j]);

    close(portal);
}

void signalHandler(int signal)
{
	switch (signal)
    {
		case SIGTERM:
		case SIGABRT:
		case SIGQUIT:
		case SIGINT:
			shutDown();
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

void setupSignals()
{
	signal(SIGTERM, signalHandler);
	signal(SIGQUIT, signalHandler);
	signal(SIGABRT, signalHandler);
	signal(SIGINT,  signalHandler);
	signal(SIGCONT, SIG_IGN);
	signal(SIGSTOP, SIG_IGN);
	signal(SIGHUP,  signalHandler);	
}

bool initialize()
{
	_terminated = false;

	return true;
}

void cleanup()
{
}

int main(int argc, char *argv[])
{
	if (!initialize())
		return 1;

	setupSignals();

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