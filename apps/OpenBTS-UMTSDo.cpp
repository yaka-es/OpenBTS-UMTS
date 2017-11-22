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
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>


#define DEFAULT_CMD_PATH "/var/run/OpenBTS-UMTS-command"

int main(int argc, char *argv[])
{
	int retcode = 0;

	const char *cmdPath = DEFAULT_CMD_PATH;
	if (argc != 1) {
		cmdPath = argv[1];
	}

	char rspPath[200];
	sprintf(rspPath, "/tmp/OpenBTS-UMTS.do.%d", getpid());

	// the socket
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
	strcpy(rspSockName.sun_path,rspPath);

	unlink(rspPath);

	if (bind(sock, (struct sockaddr *) &rspSockName, sizeof(struct sockaddr_un))) {
		perror("binding name to datagram socket");
		exit(1);
	}

	size_t input_buffer_size = 4096;
	char *inbuf = (char*)malloc(input_buffer_size);
	char *cmd = fgets(inbuf, input_buffer_size - 1, stdin);

	ssize_t nsent, nrecv;
	size_t bufsz;
	char *resbuf;

	if (cmd == NULL)
		goto done;

	cmd[strlen(cmd) - 1] = '\0';

	nsent = sendto(sock, cmd, strlen(cmd) + 1, 0,
		(const struct sockaddr *)&cmdSockName, sizeof(cmdSockName));

	if (nsent < 0) {
		perror("sending datagram");
		retcode = 1;
		goto done;
	}

	// buffer to be sized as necessary to accomodate config data length
	bufsz = 128 * 1024;
	resbuf = (char *)malloc(bufsz);
	nrecv = recv(sock, resbuf, bufsz - 1, 0);

	if (nrecv < 0) {
		perror("receiving response");
		retcode = 1;
		goto done;
	}

	resbuf[nrecv] = '\0';
	printf("%s\n", resbuf);

	free(resbuf);

done:
	close(sock);

	unlink(rspPath);

	return retcode;
}
