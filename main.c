#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define PORTAL "/dev/ttyACM1"

static volatile bool _terminated;

void shutDown()
{
	if (_terminated)
		return;

	_terminated = true;
}

void process()
{
    printf("PING\n");
}

void signalHandler(int signal)
{
	switch (signal) {
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

		sleep(5);
	}

	printf("\n");
	printf("Cleaning up!\n");

	cleanup();

	printf("All done!\n");

	return 0;
}