#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h> 
#include <termios.h>

#define PORTAL "/dev/ttyACM2"
#define BUFFER_SIZE 4096

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

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
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

void runcmd(char *cmd, int fd)
{
    char buf[BUFFER_SIZE];
    FILE *pp;

    if ((pp = popen(cmd, "r")) == NULL)
    {
        printf("Failed to open command\n");
    }
    else
    {
        while (fgets(buf, BUFFER_SIZE, pp) != NULL)
        {
            int xlen = strlen(buf);
            printf("Sending %d: \"%s\"\n", xlen, buf);

            int wlen = write(fd, buf, xlen);
            if (wlen != xlen)
            {
                printf("Error from write: %d, %d\n", wlen, errno);
            }
            tcdrain(fd);    /* delay for output */
        }

        if (pclose(pp))
        {
            printf("Command not found or exited with error status\n");
        }
    }
}

void process()
{
    printf("PING\n");

    char *portname = PORTAL;
    int fd;
    int rdpos = 0;
    char rdbuf[BUFFER_SIZE];
    char rdcmd[BUFFER_SIZE];

    fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
    if (fd < 0) 
    {
        printf("Error opening %s: %s\n", portname, strerror(errno));
        //return -1;
    }
    /*baudrate 115200, 8 bits, no parity, 1 stop bit */
    set_interface_attribs(fd, B115200);

    /* simple noncanonical input */
    while (!_terminated)
    {
        int rdlen = read(fd, rdbuf + rdpos, BUFFER_SIZE - rdpos - 1);
        if (rdlen > 0)
        {
            for (int i = 0; i < rdlen; i++)
            {
                if (rdbuf[rdpos] == 0)
                {
                    strcpy(rdcmd, rdbuf);

                    printf("Read %d: \"%s\"\n", rdpos, rdcmd);

                    runcmd(rdcmd, fd);

                    if (i + 1 < rdlen)
                        memcpy(rdbuf, rdbuf + rdpos + 1, rdpos + (rdlen - i - 1));
                    
                    rdpos = 0;
                }
                else
                {
                    rdpos++;
                }
            }
        }
        else if (rdlen == -1)
        {
            // No data available
        }
        else if (rdlen < 0)
        {
            printf("Error from read: %d: %s\n", rdlen, strerror(errno));
        }
        else
        {  /* rdlen == 0 */
            printf("Timeout from read\n");
        }

        msleep(100);
        /* repeat read to get full message */
    }

    close(fd);
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