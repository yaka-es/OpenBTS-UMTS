/*
 * OpenBTS provides an open source alternative to legacy telco protocols and
 * traditionally complex, proprietary hardware systems.
 *
 * Copyright 2011, 2012, 2013, 2014 Range Networks, Inc.
 *
 * This software is distributed under the terms of the GNU Affero General
 * See the COPYING and NOTICE files in the current or main directory for
 * directory for licensing information.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 */

// KEEP THIS FILE CLEAN FOR PUBLIC RELEASE.

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define HAVE_LIBREADLINE

#ifdef HAVE_LIBREADLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

#include <config.h>

#define DEFAULT_CMD_PATH "/var/run/OpenBTS-UMTS-command"

int main(int argc, char *argv[])
{
	printf("OpenBTS-UMTS Commnd Line Interface (CLI) utility\n");
	printf("Copyright 2012, 2013, 2014 Range Networks, Inc.\n");
	printf("Licensed under GPLv2.\n");
#ifdef HAVE_LIBREADLINE
	printf("Includes libreadline, GPLv2.\n");
#endif

	const char *cmdPath = DEFAULT_CMD_PATH;

	if (argc != 1) {
		cmdPath = argv[1];
	}

	char rspPath[200];

	sprintf(rspPath, "/tmp/OpenBTS-UMTS.console.%d.%8lx", getpid(), time(NULL));

	(void)unlink(rspPath);

	printf("command socket path is %s\n", cmdPath);

	char prompt[strlen(cmdPath) + 20];

	sprintf(prompt, "OpenBTS-UMTS> ");

	int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("opening datagram socket");
		exit(1);
	}

	// destination address
	struct sockaddr_un cmdSockName;
	cmdSockName.sun_family = AF_UNIX;
	strcpy(cmdSockName.sun_path, cmdPath);

	// locally bound address
	struct sockaddr_un rspSockName;
	rspSockName.sun_family = AF_UNIX;
	strcpy(rspSockName.sun_path, rspPath);

	if (bind(sock, (const struct sockaddr *)&rspSockName, sizeof(struct sockaddr_un))) {
		perror("binding name to datagram socket");
		exit(1);
	}

	printf("response socket bound to %s\n", rspSockName.sun_path);

#ifdef HAVE_LIBREADLINE
	// start console
	using_history();

	static const char *const history_file_name = "/.openbts-umts_history";
	char *history_name = 0;
	char *home_dir = getenv("HOME");

	if (home_dir) {
		size_t home_dir_len = strlen(home_dir);
		size_t history_file_len = strlen(history_file_name);
		size_t history_len = home_dir_len + history_file_len + 1;

		if (history_len > home_dir_len) {
			history_name = (char *)malloc(history_len);

			if (!history_name) {
				perror("malloc failed");
				exit(2);
			}

			memcpy(history_name, home_dir, home_dir_len);
			memcpy(history_name + home_dir_len, history_file_name, history_file_len + 1);
			read_history(history_name);
		}
	}
#endif

	printf("Remote Interface Ready.\n"
	       "Type:\n"
	       " \"help\" to see commands,\n"
	       " \"version\" for version information,\n"
	       " \"notices\" for licensing information.\n"
	       " \"quit\" to exit console interface\n");

	while (1) {
		char *cmd;

#ifdef HAVE_LIBREADLINE
		cmd = readline(prompt);

		if (cmd == NULL)
			break;

		if (*cmd)
			add_history(cmd);
#else  /* !HAVE_LIBREADLINE */
		printf("%s", prompt);
		fflush(stdout);

		size_t input_buffer_size = 4096;

		char *inbuf = (char *)malloc(input_buffer_size);
		cmd = fgets(inbuf, input_buffer_size - 1, stdin);

		if (cmd == NULL)
			continue;

		// strip trailing CR
		cmd[strlen(cmd) - 1] = '\0';
#endif /* HAVE_LIBREADLINE */

		// local quit?
		if (strcmp(cmd, "quit") == 0) {
			printf("closing remote console\n");
			break;
		}

		// shell escape?
		if (cmd[0] == '!') {
			int rc = system(cmd + 1);
			if (rc != 0) {
				/* nop */
			}
			continue;
		}

		// use the socket
		ssize_t ret = sendto(
			sock, cmd, strlen(cmd) + 1, 0, (const struct sockaddr *)&cmdSockName, sizeof(cmdSockName));
		if (ret < 0) {
			perror("sending datagram");
			printf("Is the remote application running?\n");
			continue;
		}

		free(cmd);

		const size_t bufsz = 128 * 1024;
		char *resbuf = (char *)malloc(bufsz);

		ssize_t nread = recv(sock, resbuf, bufsz - 1, 0);

		if (nread < 0) {
			perror("receiving response");
		} else {
			resbuf[nread] = '\0';

			printf("%s\n", resbuf);

			if (nread == (bufsz - 1))
				printf("(response truncated at %ld characters)\n", (long)nread);
		}

		free(resbuf);
	}

#ifdef HAVE_LIBREADLINE
	if (history_name) {
		int e = write_history(history_name);

		if (e) {
			fprintf(stderr, "error: history: %s\n", strerror(e));
		}

		free(history_name);
	}
#endif

	close(sock);

	// Delete the path to limit clutter in /tmp.
	(void)unlink(rspPath);
}
