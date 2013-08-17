

#include <stdio.h>
#include <stdlib.h>

#include "dynaloginclient.h"

int main(int argc, char *argv[])
{
	char *host;
	int port;
	char *user;
	char *scheme;
	char *ocra_mode;
	char code_buf[32];
	char s_challenge_string[65];
	char s_code[32];
	char c_challenge_string[65];
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
	ocra_mode = argv[5];


	session = dynalogin_session_start(host, port, NULL);

	if(session == NULL)
	{
		fprintf(stderr, "failed to get session");
		return 1;
	}
	
	if(strcasecmp(scheme,"OCRA")==0)
	{
		if(strcasecmp(ocra_mode,"one")==0)
		{
			ret = dynalogin_session_one_way_ocra_challenge(session, user, c_challenge_string);
			if(ret!=0)
				printf("failed to get challenge from server\n");
			else
				printf("Challenge is: %s\n",c_challenge_string);
		} else {	
			printf("Enter server challenge for mutual authentication\n");
			scanf("%s", s_challenge_string);
			printf("Server challenge: %s\n",s_challenge_string);
			ret = dynalogin_session_two_way_ocra_challenge(session, user, s_challenge_string, s_code, c_challenge_string);
			if(ret!=0)
				printf("Failed to get server value\n");
			else
				printf("Server's OCRA value is: %s\nClient challenge is: %s\n",s_code,c_challenge_string);

		}
	}	

	printf("Enter the code for %s: ", (char *)user);
	scanf("%s", code_buf);
	printf("\nYou entered code [%s].\n", code_buf);

	ret = dynalogin_session_authenticate(session, user, scheme, code_buf);

	printf("return value from OATH: %d\n", ret);

	dynalogin_session_stop(session);
}

