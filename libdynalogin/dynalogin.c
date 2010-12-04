/*
 * dynalogin.c
 *
 *  Created on: 23 May 2010
 *      Author: daniel
 *
 *      Implementation of the public API functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <apr_dso.h>
#include <apr_file_io.h>
#include <apr_hash.h>
#include <apr_pools.h>
#include <apr_strings.h>

#include "hotp.h"
#include "dynalogin.h"
#include "dynalogin-datastore.h"

#define DIR_SEPARATOR '/'

#define HOTP_WINDOW 20

#define CFG_LINEBUF_LEN 512

#define DYNALOGIN_PARAM_DSNAME "dynalogin.datasource"

#define GET_STRING_PARAM(r, m, s) \
	if((r = (char *)apr_hash_get(m, s, APR_HASH_KEY_STRING)) == NULL) \
		{ syslog("missing parameter"); return DYNALOGIN_ERROR; }

#define ERRMSG(s) fprintf(stderr, "%s\n", s)

/*
 * Initialise a HOTP authentication stack.
 *
 */
dynalogin_result_t dynalogin_init(dynalogin_session_t **session,
		apr_pool_t *pool, apr_hash_t *config)
{
	apr_status_t ret;
	dynalogin_session_t *h;
	char *ds_module_name;
	char *ds_module_filename;
	char *ds_sym_name;

	*session = NULL;

	if(hotp_init() != HOTP_OK)
	{
		ERRMSG("libhotp init failed");
		return DYNALOGIN_ERROR;
	}

	if(pool == NULL)
	{
		return DYNALOGIN_ERROR;
	}

	/* pool is not required, APR will create a root pool if it
	 * is NULL.
	 * Maybe we don't need a config and can run on defaults? */
	if(config == NULL)
	{
		ERRMSG("no config");
		return DYNALOGIN_ERROR;
	}

	if((h = (dynalogin_session_t *)apr_pcalloc(pool, sizeof(dynalogin_session_t)))
			== NULL)
		return DYNALOGIN_ERROR;
	h->pool = pool;

	fprintf(stderr, "looking in %s for modules\n", PKGLIBDIR);

	GET_STRING_PARAM(ds_module_name, config, DYNALOGIN_PARAM_DSNAME)

	ds_module_filename = apr_psprintf(h->pool, "%s%c%s.so",
			PKGLIBDIR, DIR_SEPARATOR, ds_module_name);
	ds_sym_name = apr_psprintf(h->pool, "%s_module", ds_module_name);
	if(ds_module_filename==NULL || ds_sym_name==NULL)
		return DYNALOGIN_ERROR;

	if(apr_dso_load(&h->dso_handle,
			ds_module_filename, h->pool)!=APR_SUCCESS)
		return DYNALOGIN_ERROR;

	if(apr_dso_sym((apr_dso_handle_sym_t *)&(h->datasource), h->dso_handle, ds_sym_name)
			!=APR_SUCCESS)
	{
		apr_dso_unload(h->dso_handle);
		return DYNALOGIN_ERROR;
	}

	if(h->datasource->init != NULL)
		if(h->datasource->init(h->pool, config) != DYNALOGIN_SUCCESS)
		{
			apr_dso_unload(h->dso_handle);
			return DYNALOGIN_ERROR;
		}

	*session = h;

	return DYNALOGIN_SUCCESS;
}

void dynalogin_done(dynalogin_session_t *h)
{
	if(h==NULL)
		return;
	if(h->datasource->done != NULL)
		h->datasource->done();
	apr_dso_unload(h->dso_handle);
	hotp_done();
}

dynalogin_result_t dynalogin_authenticate
	(dynalogin_session_t *h, const dynalogin_userid_t userid,
			const dynalogin_code_t code)
{
	int rc;
	dynalogin_user_data_t *ud;
	dynalogin_result_t res;

	if(h == NULL || userid == NULL || code == NULL)
		return DYNALOGIN_ERROR;

	h->datasource->user_fetch(&ud, userid, h->pool);
	if(ud == NULL)
	{
		ERRMSG("userid not found");
		fprintf(stderr, "userid was %s\n", userid);
		return DYNALOGIN_ERROR;
	}

	rc = hotp_validate_otp (
			ud->secret,
			strlen(ud->secret),
			ud->counter,
			HOTP_WINDOW,
			code);
	if(rc < 0)
	{
		ud->failure_count++;
		res = DYNALOGIN_DENY;
	}
	else
	{
		ud->counter += (rc + 1);
		ud->failure_count = 0;
		time(&ud->last_success);
		ud->last_code = code;
		res = DYNALOGIN_SUCCESS;
	}
	time(&ud->last_attempt);
	h->datasource->user_update(ud, h->pool);
	return res;
}

dynalogin_result_t dynalogin_read_config_from_file(apr_hash_t **config,
		const char *filename, apr_pool_t *pool)
{
	apr_hash_t *_config = NULL;
	apr_status_t res;
	apr_file_t *f;
	char buf[CFG_LINEBUF_LEN + 1];
	size_t len;
	int i;
	char *key, *val;

	*config = NULL;

	if((_config=apr_hash_make(pool)) == NULL)
		return DYNALOGIN_ERROR;

	if(res=apr_file_open(&f, filename, APR_READ | APR_SHARELOCK, 0, pool)
			!= APR_SUCCESS)
		return DYNALOGIN_ERROR;

	while(apr_file_gets(buf, CFG_LINEBUF_LEN, f) == APR_SUCCESS)
	{
		len = strlen(buf);
		if(len > 0 && buf[len-1]=='\n')
			buf[--len] = 0;

		for(i = 0; i < len && buf[i] != '='; i++);

		if(buf[i] == '=' && i > 0)
		{
			buf[i] = 0;
			if((val = apr_pstrdup(pool, &buf[i+1])) == NULL)
			{
				apr_file_close(f);
				return DYNALOGIN_ERROR;
			}
			if((key = apr_pstrdup(pool, buf))==NULL)
			{
				apr_file_close(f);
				return DYNALOGIN_ERROR;
			}
			apr_hash_set(_config, key, APR_HASH_KEY_STRING, val);
		}
	}

	apr_file_close(f);

	*config = _config;

	return DYNALOGIN_SUCCESS;
}
