

#include <stdio.h>
#include <stdlib.h>

#include "dynaloginclient.h"

int main(int argc, char *argv[])
{
	char *host;
	int port;
	char *user;
	char *scheme;
	char code_buf[32];
	char challenge_string[65];
	dynalogin_client_t *session;
	int ret;

	if(argc < 5)
	{
		fprintf(stderr, "please specify host, port, user and scheme");
		return 1;
	}

	host = argv[1];
	port = atoi(argv[2]);
	user = argv[3];
	scheme = argv[4];

	session = dynalogin_session_start(host, port, NULL);

	if(session == NULL)
	{
		fprintf(stderr, "failed to get session");
		return 1;
	}
	
	if(strcasecmp(scheme,"OCRA")==0)
	{
		ret = dynalogin_session_one_way_ocra_challenge(session, user, challenge_string);
		if(ret!=0)
			printf("failed to get challenge from server\n");
		else
			printf("challenge is: %s\n",challenge_string);
	}

	printf("Enter the code for %s: ", (char *)user);
	scanf("%s", code_buf);
	printf("\nYou entered code [%s].\n", code_buf);

	ret = dynalogin_session_authenticate(session, user, scheme, code_buf);

	printf("return value from OATH: %d\n", ret);

	dynalogin_session_stop(session);
}

