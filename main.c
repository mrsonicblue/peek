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

#define PORTAL "/dev/ttyACM1"

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

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

void set_mincount(int fd, int mcount)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0)
    {
        printf("Error tcgetattr: %s\n", strerror(errno));
        return;
    }

    tty.c_cc[VMIN] = mcount ? 1 : 0;
    tty.c_cc[VTIME] = 5;        /* half second timer */

    if (tcsetattr(fd, TCSANOW, &tty) < 0)
        printf("Error tcsetattr: %s\n", strerror(errno));
}

void process()
{
    printf("PING\n");

    char *portname = PORTAL;
    int fd;
    int wlen;
    char *xstr = "Hello!\r\n";
    int xlen = strlen(xstr);

    fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) 
    {
        printf("Error opening %s: %s\n", portname, strerror(errno));
        //return -1;
    }
    /*baudrate 115200, 8 bits, no parity, 1 stop bit */
    set_interface_attribs(fd, B115200);
    set_mincount(fd, 0);                /* set to pure timed read */

    /* simple output */
    wlen = write(fd, xstr, xlen);
    if (wlen != xlen)
    {
        printf("Error from write: %d, %d\n", wlen, errno);
    }
    tcdrain(fd);    /* delay for output */

    /* simple noncanonical input */
    while (!_terminated)
    {
        unsigned char buf[80];
        int rdlen;

        rdlen = read(fd, buf, sizeof(buf) - 1);
        if (rdlen > 0)
        {
            buf[rdlen] = 0;
            printf("Read %d: \"%s\"\n", rdlen, buf);
        }
        else if (rdlen < 0)
        {
            printf("Error from read: %d: %s\n", rdlen, strerror(errno));
        }
        else
        {  /* rdlen == 0 */
            printf("Timeout from read\n");
        }

        sleep(1);
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

		sleep(1);
	}

	printf("\n");
	printf("Cleaning up!\n");

	cleanup();

	printf("All done!\n");

	return 0;
}