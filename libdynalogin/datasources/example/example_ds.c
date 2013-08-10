/*
 * test_ds.c
 *
 */

#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <apr_hash.h>

#include "dynalogin-datastore.h"

extern dynalogin_datastore_module_t example_ds_module;

static dynalogin_user_data_t u_tester =
{
		"testuser",  // name
		OCRA,        // scheme, can be HOTP or TOTP
		"12345678901234567890", // secret from RFC 4226 test vector
		1, // counter
        "OCRA-1:HOTP-SHA1-8:C-QH20-T05M",
        "09876543210987654321", //server secret
        "OCRA-1:HOTP-SHA1-8:QH08", //server ocra suite
        NULL,
        NULL,
		0, // failure_count
		0, // locked
		0, // last_success
		0, // last_attempt
		"", // last generated code
		NULL, // password
		NULL // pvt
};

static dynalogin_result_t init(apr_pool_t *pool, apr_hash_t *config)
{
	syslog(LOG_NOTICE, "test_ds: init");
}

static void done(void)
{
}

static void user_fetch(dynalogin_user_data_t **ud,
		const dynalogin_userid_t userid,
		apr_pool_t *pool)
{
	if(strcmp((char *)userid, u_tester.userid) == 0)
		*ud = &u_tester;
	else
		*ud = (dynalogin_user_data_t *)NULL;

	syslog(LOG_DEBUG, "user = %s, count = %d", userid, u_tester.counter);
	return;
}

static void user_update(dynalogin_user_data_t *ud, apr_pool_t *pool)
{
	syslog(LOG_DEBUG, "updated data: user = %s, count = %d", ud->userid, ud->counter);
	return;
}

dynalogin_datastore_module_t example_ds_module =
{
		init,
		done,
		NULL, // user_add
		NULL, // user_delete
		NULL, // user_update_secret
		user_fetch, // user_fetch
		user_update, // user_update
		NULL,
		NULL  // dynalogin_pvt - used by libdynalogin
};
