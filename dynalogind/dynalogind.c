/*
 * dynalogind.c
 *
 *  Created on: 29 May 2010
 *      Author: daniel
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <syslog.h>

#include <apr_file_io.h>
#include <apr_network_io.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>

#include "dynalogin.h"

#define ERRBUFLEN 1024

#define DEFAULT_CONFIG_FILENAME "dynalogind.conf"

typedef struct socket_thread_data_t {
	apr_pool_t *pool;
	apr_socket_t *socket;
	dynalogin_session_t *dynalogin_session;
} socket_thread_data_t;

apr_status_t read_line(apr_pool_t *pool, apr_socket_t *socket,
		char **buf, apr_size_t bufsize)
{
	char errbuf[ERRBUFLEN + 1];
	apr_status_t res;
	char *_buf;
	apr_size_t readsize = bufsize, i;

	if((*buf = apr_pcalloc(pool, bufsize))==NULL)
	{
		syslog(LOG_ERR, "read_line: apr_pcalloc failed");
		apr_pool_destroy(pool);
		return;
	}
	_buf = *buf;

	res = apr_socket_recv(socket, _buf, &readsize);

	if(res == APR_SUCCESS || res == APR_EOF)
	{
		_buf[readsize] = 0;
		for(i = 0; i < readsize; i++)
			if(_buf[i] == '\r' || _buf[i] == '\n')
				_buf[i] = 0;
	}
	else
		syslog(LOG_ERR, "unexpected result while reading socket: %s",
				apr_strerror(res, errbuf, ERRBUFLEN));

	return res;
}

apr_status_t send_answer(apr_socket_t *socket, const char *answer)
{
	apr_size_t msglen;
	apr_size_t sent;
	apr_size_t total_sent = 0;
	apr_status_t res;

	msglen = strlen(answer);
	while(total_sent < msglen)
	{
		sent = msglen;
		res = apr_socket_send(socket, answer + total_sent, &sent);
		if(res != APR_SUCCESS)
			return res;
		total_sent += sent;
	}
	return APR_SUCCESS;
}

void socket_thread_handle(socket_thread_data_t *td)
{
	char *buf;
	char errbuf[ERRBUFLEN + 1];
	apr_size_t bufsize = 1024;
	apr_size_t readsize = bufsize;
	char **argv;
	apr_status_t res;
	dynalogin_result_t dynalogin_res;

	apr_pool_t *query_pool;

	dynalogin_userid_t userid;
	dynalogin_code_t code;

	if((res=apr_pool_create(&query_pool, td->pool))!=APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to create query pool: %s",
						apr_strerror(res, errbuf, ERRBUFLEN));
		return;
	}

	readsize = bufsize;
	res = read_line(query_pool, td->socket, &buf, bufsize);
	while(res == APR_SUCCESS || res == APR_EOF)
	{
		if((res=apr_tokenize_to_argv(buf, &argv, query_pool))
				!=APR_SUCCESS)
		{
			syslog(LOG_ERR, "failed to tokenize query: %s",
							apr_strerror(res, errbuf, ERRBUFLEN));
			return;
		}

		if(argv[0] == NULL || argv[1] == NULL)
		{
			syslog(LOG_WARNING, "insufficient tokens in query");
			return;
		}
		userid=argv[0];
		code=argv[1];
		syslog(LOG_DEBUG, "attempting DYNALOGIN auth for user=%s", userid);
		dynalogin_res = dynalogin_authenticate(td->dynalogin_session,
				userid, code);

		switch(dynalogin_res)
		{
		case DYNALOGIN_SUCCESS:
			syslog(LOG_DEBUG, "DYNALOGIN success for user=%s", userid);
			res = send_answer(td->socket, "AUTH_SUCCESS\n");
			break;
		case DYNALOGIN_DENY:
			syslog(LOG_DEBUG, "DYNALOGIN denied for user=%s", userid);
			res = send_answer(td->socket, "AUTH_DENY\n");
			break;
		case DYNALOGIN_ERROR:
			syslog(LOG_DEBUG, "DYNALOGIN error for user=%s", userid);
			res = send_answer(td->socket, "AUTH_ERROR\n");
			break;
		default:
			syslog(LOG_DEBUG, "DYNALOGIN unexpected result for user=%s", userid);
			res = send_answer(td->socket, "AUTH_ERROR\n");
		}

		if(res != APR_SUCCESS)
		{
			syslog(LOG_ERR, "failed to send response: %s",
							apr_strerror(res, errbuf, ERRBUFLEN));
			return;
		}

		apr_pool_destroy(query_pool);
		if((res=apr_pool_create(&query_pool, td->pool))!=APR_SUCCESS)
		{
			syslog(LOG_ERR, "failed to create query pool: %s",
							apr_strerror(res, errbuf, ERRBUFLEN));
			return;
		}
		res = read_line(query_pool, td->socket, &buf, bufsize);
	}
	syslog(LOG_ERR, "failed to read input from socket: %s",
					apr_strerror(res, errbuf, ERRBUFLEN));
}

void socket_thread_main(apr_thread_t *self, void *data)
{
	socket_thread_data_t *thread_data = (socket_thread_data_t*)data;

	socket_thread_handle(thread_data);

	apr_socket_close(thread_data->socket);
	apr_pool_destroy(thread_data->pool);
	syslog(LOG_INFO, "client connection closed");
}

apr_status_t handle_new_client(apr_socket_t *socket, apr_pool_t *pool,
		dynalogin_session_t *h)
{
	char buf[ERRBUFLEN + 1];

	apr_status_t res;
	apr_threadattr_t *t_attr;
	apr_thread_t *t;
	apr_pool_t *subpool;
	socket_thread_data_t *thread_data;

	res = apr_pool_create(&subpool, pool);
	if(res != APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to create pool: %s",
				apr_strerror(res, buf, ERRBUFLEN));
		apr_socket_close(socket);
		return res;
	}

	res = apr_threadattr_create(&t_attr, subpool);
	if(res != APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to create threadattr: %s",
				apr_strerror(res, buf, ERRBUFLEN));
		apr_pool_destroy(subpool);
		apr_socket_close(socket);
		return res;
	}

	thread_data = apr_pcalloc(subpool, sizeof(struct socket_thread_data_t));
	if(thread_data == NULL)
	{
		syslog(LOG_ERR, "handle_new_client: apr_pcalloc failed");
		apr_pool_destroy(subpool);
		apr_socket_close(socket);
		return res;
	}

	thread_data->pool = subpool;
	thread_data->socket = socket;
	thread_data->dynalogin_session = h;

	res = apr_thread_create(&t, t_attr,
			(apr_thread_start_t)socket_thread_main, thread_data, subpool);

	if(res != APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to spawn a thread: %s",
				apr_strerror(res, buf, ERRBUFLEN));
		apr_pool_destroy(subpool);
		apr_socket_close(socket);
	}

	return res;
}

int main(int argc, char *argv[])
{
	char errbuf[ERRBUFLEN + 1];
	apr_pool_t *pool;
	apr_proc_t proc;
	dynalogin_session_t *h;
	apr_status_t res;

	apr_sockaddr_t *sa;
	apr_socket_t *socket, *socket_new;

	char *cfg_filename;
	char *bind_address = "0.0.0.0";
	int bind_port = 9050;
	int qlen = 32;

	int done = 0;

	apr_hash_t *config;

	if(apr_initialize() != APR_SUCCESS)
	{
		fprintf(stderr, "apr_initialize failed\n");
		return 1;
	}

	openlog(argv[0], LOG_PID, LOG_AUTHPRIV);

	if((res = apr_pool_create(&pool, NULL)) != APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to create root pool: %s",
				apr_strerror(res, errbuf, ERRBUFLEN));
		return 1;
	}

	cfg_filename = apr_psprintf(pool, "%s%c%s",
			SYSCONFDIR, DEFAULT_CONFIG_FILENAME);
	if(cfg_filename == NULL)
	{
		syslog(LOG_ERR, "apr_psprintf failed to create filename: %s",
				apr_strerror(res, errbuf, ERRBUFLEN));
		return 1;
	}
	/* Read config */
	if(dynalogin_read_config_from_file(&config, cfg_filename, pool)
			!= DYNALOGIN_SUCCESS)
	{
		syslog(LOG_ERR, "failed to read config file %s");
		return 1;
	}

	/* Set up DYNALOGIN session (threadsafe?) */
	if(dynalogin_init(&h, pool, config) != DYNALOGIN_SUCCESS)
	{
		syslog(LOG_ERR, "failed to init dynalogin stack");
		return 1;
	}

	/* Daemonize */
	if((res=apr_proc_detach(0)) != APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to detach: %s",
				apr_strerror(res, errbuf, ERRBUFLEN));
		return 1;
	}

	/* Create socket for clients */
	if((res=apr_socket_create(&socket,
			APR_INET, SOCK_STREAM, APR_PROTO_TCP, pool))!=APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to create listening socket: %s",
				apr_strerror(res, errbuf, ERRBUFLEN));
		return 1;
	}
	if((res=apr_sockaddr_info_get(&sa, bind_address, APR_UNSPEC,
			bind_port, APR_IPV4_ADDR_OK || APR_IPV6_ADDR_OK, pool))!=APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to resolve bind address: %s",
				apr_strerror(res, errbuf, ERRBUFLEN));
		apr_socket_close(socket);
		return 1;
	}
	if((res=apr_socket_opt_set(socket, APR_SO_REUSEADDR, 1))!=APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to set APR_SO_REUSEADDR: %s",
			apr_strerror(res, errbuf, ERRBUFLEN));
		apr_socket_close(socket);
		return 1;
	}
	if((res=apr_socket_bind(socket, sa))!=APR_SUCCESS)
	{
		syslog(LOG_ERR, "failed to bind: %s",
				apr_strerror(res, errbuf, ERRBUFLEN));
		apr_socket_close(socket);
		return 1;
	}

	/* Main loop */
	while(done != 1)
	{
		if((res=apr_socket_listen(socket, qlen))!=APR_SUCCESS)
		{
			syslog(LOG_ERR, "failed apr_socket_listen: %s",
					apr_strerror(res, errbuf, ERRBUFLEN));
			apr_socket_close(socket);
			return 1;
		}

		if((res=apr_socket_accept(&socket_new, socket, pool))!=APR_SUCCESS)
		{
			syslog(LOG_ERR, "failed to accept incoming connection: %s",
					apr_strerror(res, errbuf, ERRBUFLEN));
			apr_socket_close(socket);
			return 1;
		}
		syslog(LOG_INFO, "new incoming connection");
		if((res=handle_new_client(socket_new, pool, h))!=APR_SUCCESS)
		{
			syslog(LOG_ERR, "failed to handle incoming connection: %s",
					apr_strerror(res, errbuf, ERRBUFLEN));
			apr_socket_close(socket);
			return 1;
		}
	}
	apr_socket_close(socket);
	return 0;
}