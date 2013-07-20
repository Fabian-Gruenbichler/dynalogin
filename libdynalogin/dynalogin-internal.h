/*

 */

#ifndef DYNALOGIN_INTERNAL_H_
#define DYNALOGIN_INTERNAL_H_

struct oath_callback_pvt_t
{
	apr_pool_t *pool;
	const void *extra;
    const char *password;
    char *validated_code;
    const char *challenge;
    size_t challenge_length;
};

#endif /* DYNALOGIN_INTERNAL_H_ */
