#include <sys/compiler.h>
#include <sys/log.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <list.h>
#include <net/tls/conf.h>

#include <aaa/lib.h>
#include <aaa/prv.h>

void
aaa_config_load(struct aaa *c)
{
	_unused const char *file = getenv("OPENAAA_CONF");
}
