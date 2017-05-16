/*
 * $id: mod_openaaa.c                               Daniel Kubec <niel@rtfm.cz>
 */

#undef HAVE_STRING_H
#undef PACKAGE_NAME
#undef PACKAGE_VERSION

#include <stdlib.h>
#include <unistd.h>
#include <ap_config.h>
#include <ap_socache.h>
#include <apr_strings.h>
#include <httpd.h>
#include <http_config.h>
#include <http_connection.h>
#include <http_core.h>
#include <http_log.h>
#include <http_main.h>
#include <http_request.h>
#include <http_protocol.h>
#include <util_filter.h>
#include <util_script.h>

#include "httpd.h"
#include "http_config.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_core.h"

#include "mod_ssl.h"
#include "mod_ssl_openssl.h"

#include "mod_openaaa.h"
#include "private.h"

/* AAA abstraction */
#include <mem/stack.h>
#include <aaa/lib.h>
#include <crypto/sha1.h>
#include <crypto/hex.h>
#include <crypto/abi/ssl.h>

#include <sys/log.h>

APLOG_USE_MODULE(aaa);

APR_OPTIONAL_FN_TYPE(ssl_is_https)         *ssl_is_https;
APR_OPTIONAL_FN_TYPE(ssl_var_lookup)       *ssl_var_lookup;

/*
 * Gives modules a chance to create their request_config entry when the
 * request is created.
 * @param r The current request
 * @ingroup hooks
 */

static int
create_request(request_rec *r);

static apr_status_t
destroy_request(void *ctx);

static void
log_write(struct log_ctx *ctx, const char *msg, int len)
{
	server_rec *s = (server_rec *)ctx->user;
	ap_log_error(ctx->file, ctx->line, APLOG_MODULE_INDEX, APLOG_INFO, 0, s, msg);
}

/*
 * Run the child_init functions for each module
 * @param pchild The child pool
 * @param s The list of server_recs in this server
 */

static void
child_init(apr_pool_t *p, server_rec *s)
{
	log_verbose = 4;
	log_custom_set(log_write, s);

	struct aaa *a = aaa_new(AAA_ENDPOINT_SERVER, 0);

	for (; s; s = s->next) {
		struct srv *srv;
		srv = ap_get_module_config(s->module_config, &MODULE_ENTRY);
		srv->pid = getpid();
		srv->aaa = a;
		srv->mod_ssl = ap_find_linked_module("mod_ssl.c");
	}

	apr_pool_cleanup_register(p, a, child_fini, child_fini);
}

/*
 * Run the child_fini functions for each module
 * @param ctx The ctxt in this server
 */

static apr_status_t
child_fini(void *ctx)
{
	struct aaa *a = (struct aaa*)ctx;
	aaa_free(a);
	return 0;
}

/*
 * Run the post_config function for each module
 * @param pconf The config pool
 * @param plog The logging streams pool
 * @param ptemp The temporary pool
 * @param s The list of server_recs
 * @return OK or DECLINED on success anything else is a error
 */

static int
post_config(apr_pool_t *p, apr_pool_t *l, apr_pool_t *t, server_rec *s)
{
	struct srv *srv = ap_get_module_config(s->module_config, &MODULE_ENTRY);
	ap_add_version_component(p, MODULE_VERSION);
	return OK;
}

static void
optional_fn_retrieve(void)
{
	ssl_is_https        = APR_RETRIEVE_OPTIONAL_FN(ssl_is_https);
	ssl_var_lookup      = APR_RETRIEVE_OPTIONAL_FN(ssl_var_lookup);
}

/*
 * Gives modules a chance to create their request_config entry when the
 * request is created.
 * @param r The current request
 * @ingroup hooks
 */

static int
create_request(request_rec *r)
{
	struct srv *srv = ap_srv_config_get(r->server);
	struct req *req = apr_pcalloc(r->pool, sizeof(*req));
	req->r = r;
	ap_req_config_set(r, req);
	apr_pool_cleanup_register(r->pool, r, destroy_request,destroy_request);

	return DECLINED;
}

static apr_status_t
destroy_request(void *ctx)
{
	request_rec *r = ctx;
	return DECLINED;
}

/*
 * This hook gives protocol modules an opportunity to set everything up
 * before calling the protocol handler.  All pre-connection hooks are
 * run until one returns something other than ok or decline
 * @param c The connection on which the request has been received.
 * @param csd The mechanism on which this connection is to be read.
 *            Most times this will be a socket, but it is up to the module
 *            that accepts the request to determine the exact type.
 * @return OK or DECLINED
 */

static int
pre_connection(conn_rec *c, void *csd)
{
	//modssl_register_npn(c, npn_advertise_protos, npn_proto_negotiated);
	return DECLINED;
}

/*
 * This hook allows modules to affect the request immediately after the request
 * has been read, and before any other phases have been processes.  This allows
 * modules to make decisions based upon the input header fields
 * @param r The current request
 * @return OK or DECLINED
 */

static int
post_read_request(request_rec *r)
{
	if (!ap_is_initial_req(r))
		return DECLINED;
	if (!ssl_is_https)
		return DECLINED;
	if (!ssl_is_https(r->connection))
		return DECLINED;

	r_info(r, "%s() uri: %s", __func__, r->uri);
	struct srv *srv = ap_srv_config_get(r->server);
	struct aaa *aaa = srv->aaa;

	char ssl_id[1024];
	ssl_get_sess_id(srv->ssl, ssl_id, sizeof(ssl_id) - 1);

	aaa_reset(aaa);
	aaa_attr_set(aaa, "sess.id", (char *)ssl_id);
	if (aaa_bind(aaa) < 0)
		return DECLINED;

	const char *uid = aaa_attr_get(aaa, "user.id");
	if (!uid || !*uid)
		return DECLINED;

	/* increase session expiration for authenticated sessions */
	aaa_touch(aaa);
	aaa_commit(aaa);

	return DECLINED;

}

/**
 * Register a hook function that will analyze the request headers,
 * authenticate the user, and set the user information in the request record.
 * @param pf A check_user_id hook function
 * @param aszPre A NULL-terminated array of strings that name modules whose
 *               hooks should precede this one
 * @param aszSucc A NULL-terminated array of strings that name modules whose
 *                hooks should succeed this one
 * @param nOrder An integer determining order before honouring aszPre and
 *               aszSucc (for example, HOOK_MIDDLE)
 * @param type Internal request processing mode, either
 *             AP_AUTH_INTERNAL_PER_URI or AP_AUTH_INTERNAL_PER_CONF
 */

static int
check_authn(request_rec *r)
{
	r_info(r, "%s() type:%s uri: %s", __func__, ap_auth_type(r), r->uri);
	if (!ap_auth_type(r) || strcasecmp(ap_auth_type(r), "openaaa"))
		return DECLINED;

	struct req *req = ap_req_config_get(r);
	struct srv *srv = ap_srv_config_get(r->server);
	struct aaa *aaa = srv->aaa;

	const char *sess_id = aaa_attr_get(aaa, "sess.id");
	const char *user_id = aaa_attr_get(aaa, "user.id");
	const char *user_name = aaa_attr_get(aaa, "user.name");

	r_info(r, "sess.id: %s", sess_id);
	if (!user_id) {
		apr_table_set(r->subprocess_env, "SCRIPT_REFERER", r->uri);
		r_info(r, "%s() SCRIPT_REFERER=%s", __func__, r->uri);
		r_info(r, "%s() HTTP_FORBIDDEN", __func__);
		return HTTP_FORBIDDEN;
	}

	r_info(r, "user.id: %s", user_id);
	if (user_name) r_info(r, "user.name: %s", user_name);

	r->user = apr_pstrdup(r->pool, user_name ? user_name : user_id);
	apr_table_set(r->subprocess_env, "REMOTE_USER", r->user);
	return OK;
}

/*
 * This hook is used to apply additional access control to this resource.
 * It runs *before* a user is authenticated, so this hook is really to
 * apply additional restrictions independent of a user. It also runs
 * independent of 'Require' directive usage.
 *
 * @param r the current request
 * @return OK, DECLINED, or HTTP_...
 * @ingroup hooks
 */

static int
access_checker(request_rec *r)
{
	if (!ap_auth_type(r) || strcasecmp(ap_auth_type(r), "openaaa"))
		return DECLINED;

	r_info(r, "%s() uri: %s", __func__, r->uri);
	return OK;
}

/*
 * Register a hook function that will apply additional access control to
 * the current request.
 * @param pf An access_checker hook function
 * @param aszPre A NULL-terminated array of strings that name modules whose
 *               hooks should precede this one
 * @param aszSucc A NULL-terminated array of strings that name modules whose
 *                hooks should succeed this one
 * @param nOrder An integer determining order before honouring aszPre and
 *               aszSucc (for example, HOOK_MIDDLE)
 * @param type Internal request processing mode, either
 *             AP_AUTH_INTERNAL_PER_URI or AP_AUTH_INTERNAL_PER_CONF
 */

static int
check_access(request_rec *r)
{
	r_info(r, "%s() type:%s uri: %s", __func__, ap_auth_type(r), r->uri);
	if (!ap_auth_type(r) || strcasecmp(ap_auth_type(r), "openaaa"))
		return DECLINED;

	return OK;
}

/*
 * This hook is used to check to see if the resource being requested
 * is available for the authenticated user (r->user and r->ap_auth_type).
 * It runs after the access_checker and check_user_id hooks. Note that
 * it will *only* be called if Apache determines that access control has
 * been applied to this resource (through a 'Require' directive).
 *
 * @param r the current request
 * @return OK, DECLINED, or HTTP_...
 * @ingroup hooks
 */

static int
auth_checker(request_rec *r)
{
	r_info(r, "%s() type:%s uri: %s", __func__, ap_auth_type(r), r->uri);
	if (!ap_auth_type(r) || strcasecmp(ap_auth_type(r), "openaaa"))
		return DECLINED;

	struct req *req = ap_req_config_get(r);
	struct srv *srv = ap_srv_config_get(r->server);
	struct aaa *aaa = srv->aaa;

	const char *sess_id = aaa_attr_get(aaa, "sess.id");
	const char *user_id = aaa_attr_get(aaa, "user.id");
	const char *user_name = aaa_attr_get(aaa, "user.name");

	r_info(r, "sess.id: %s", sess_id);
	if (!user_id)
		return HTTP_FORBIDDEN;

	r_info(r, "user.id: %s", user_id);
	if (user_name) r_info(r, "user.name: %s", user_name);

	r->user = apr_pstrdup(r->pool, user_name ? user_name : user_id);
	apr_table_add(r->subprocess_env, "REMOTE_USER", r->user);
	return OK;
}

/*
 * At this step all modules can have a last chance to modify the 
 * response header (for example set a cookie) before the calling 
 * the content handler.
 *
 * @param r The current request
 * @return OK, DECLINED, or HTTP_...
 * @ingroup hooks
 */

static int
fixups(request_rec *r)
{
	r_info(r, "%s() type:%s uri: %s", __func__, ap_auth_type(r), r->uri);
	if (!ap_auth_type(r) || strcasecmp(ap_auth_type(r), "openaaa"))
		return DECLINED;

	struct srv *srv = ap_srv_config_get(r->server);
	struct aaa *aaa = srv->aaa;

	const char *user_id   = aaa_attr_get(aaa, "user.id");
	const char *user_name = aaa_attr_get(aaa, "user.name");

	r->user = apr_pstrdup(r->pool, user_name ? user_name : user_id);
	apr_table_add(r->subprocess_env, "REMOTE_USER", r->user);

	return OK;
}

static void *
config_init_srv(apr_pool_t *p, server_rec *s)
{
	struct srv *srv = apr_pcalloc(p, sizeof(*srv));
	return srv;
}

static void *
config_init_dir(apr_pool_t *p, char *d)
{
	struct dir *dir = apr_pcalloc(p, sizeof(*dir));
	return dir;
}

static const char *
config_tls(cmd_parms *cmd, void *d, const char *v)
{
	struct srv *s = ap_srv_config_get_cmd(cmd);

	const char *arg;
	const char *word = ap_getword_conf(cmd->pool, &arg);
	if (!strncasecmp(word, "plus", 1)) {
		word = ap_getword_conf(cmd->pool, &arg);
	};

	return ap_check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE);
}

/*
 * Labels here have the same definition as in TLS, i.e., an ASCII string
 * with no terminating NULL.  Label values beginning with "EXPERIMENTAL"
 * MAY be used for private use without registration.  All other label
 * values MUST be registered via Specification Required as described by
 * RFC 5226 [RFC5226].  Note that exporter labels have the potential to
 * collide with existing PRF labels.  In order to prevent this, labels
 * SHOULD begin with "EXPORTER".  This is not a MUST because there are
 * existing uses that have labels which do not begin with this prefix.
 */

static const char *
config_aaa_label(cmd_parms *cmd, void *d, const char *v)
{
	struct srv *s = ap_srv_config_get_cmd(cmd);
	s->keymat_label = v;
	return ap_check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE);
}

/*
 * Keying Material Exporter bytes (default 20)
 */

static const char *
config_aaa_len(cmd_parms *cmd, void *d, const char *v)
{
	struct srv *s = ap_srv_config_get_cmd(cmd);
	s->keymat_len = atoi(v);
	return ap_check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE);
}

/*
 * The command record structure.  Each modules can define a table of these
 * to define the directives it will implement.
 */

static const command_rec cmds[] = {
	AP_INIT_TAKE1("TLS", config_tls, NULL, RSRC_CONF,
	              "Configures multiple TLS options"),
	AP_INIT_TAKE1("TLSKeyingMaterialLabel", config_aaa_label, NULL, RSRC_CONF,
	              "Labels here have the same definition as in TLS"),
	AP_INIT_TAKE1("TLSKeyingMaterialLength", config_aaa_len, NULL, RSRC_CONF,
	              "Export len bytes of keying material (default 20)"),	
	{NULL},
};
/**
 * init_server hook -- allow SSL_CTX-specific initialization to be performed by
 * a module for each SSL-enabled server (one at a time)
 * @param s SSL-enabled [virtual] server
 * @param p pconf pool
 * @param is_proxy 1 if this server supports backend connections
 * over SSL/TLS, 0 if it supports client connections over SSL/TLS
 * @param ctx OpenSSL SSL Context for the server
 */
	
static int
init_server(server_rec *s, apr_pool_t *p, int is_proxy, SSL_CTX *ctx)
{
	ssl_init();
	ssl_init_ctxt(ctx);	
	return 0;
}

/**
 * pre_handshake hook
 * @param c conn_rec for new connection from client or to backend server
 * @param ssl OpenSSL SSL Connection for the client or backend server
 * @param is_proxy 1 if this handshake is for a backend connection, 0 otherwise
 */

static int
pre_handshake(conn_rec *c, SSL *ssl, int is_proxy)
{
	struct srv *srv = ap_srv_config_get(c->base_server);
	srv->ssl = ssl;

	ssl_init_conn(ssl);
	return 0;
}

/**
 * proxy_post_handshake hook -- allow module to abort after successful
 * handshake with backend server and subsequent peer checks
 * @param c conn_rec for connection to backend server
 * @param ssl OpenSSL SSL Connection for the client or backend server
 */

static int
proxy_post_handshake(conn_rec *c, SSL *ssl)
{
	debug2("proxy_post_handshake");
	return 0;
}

static int
header_parser(request_rec *r)
{
	if (!ssl_is_https)
		return DECLINED;
	if (!ssl_is_https(r->connection))
		return DECLINED;
	if (!ap_auth_type(r) || strcasecmp(ap_auth_type(r), "openaaa"))
		return DECLINED;

	const char *ref = r->main ? r->main->uri: r->prev ? r->prev->uri: NULL;
	
	struct req *req = ap_req_config_get(r);
	struct srv *srv = ap_srv_config_get(r->server);
	struct aaa *aaa = srv->aaa;

	r_info(r, "%s() uri=%s", __func__, r->uri);

	for (const char *k = aaa_attr_first(aaa, ""); k; k = aaa_attr_next(aaa)) {
		char *key = printfa("aaa.%s", k);
		for (char *p = key; *p; p++)
			if ((*p = toupper(*p)) == '.') *p = '_';

		const char *val = aaa_attr_get(aaa, k);
		r_info(r, "%s() %s:%s", __func__, k, val);	
		apr_table_set(r->subprocess_env, key, val);
	}

	const char *uid  = aaa_attr_get(aaa, "user.id");
	const char *name = aaa_attr_get(aaa, "user.name");

	if (uid) {
		const char *user = apr_pstrdup(r->pool, name ? name: uid);
		apr_table_add(r->subprocess_env, "REMOTE_USER", user);
	}
	
	if (!ref)
		return OK;

	r_info(r, "%s() script referer=%s", __func__, ref);
	apr_table_set(r->subprocess_env, "SCRIPT_REFERER", ref);

	return OK;
}

static void
register_hooks(apr_pool_t *p)
{
	/* pre_connection hook needs to run after mod_ssl connection hook. */
	static const char *pre_ssl[] = { "mod_ssl.c", NULL };
	/* make sure we run before mod_rewrite's handler */
	static const char *const asz_succ[] = { "mod_setenvif.c", 
	                                        "mod_rewrite.c", NULL };

	ap_hook_child_init(child_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_optional_fn_retrieve(optional_fn_retrieve, NULL,NULL,APR_HOOK_MIDDLE);
	ap_hook_header_parser(header_parser, NULL, asz_succ, APR_HOOK_MIDDLE);
	ap_hook_post_config(post_config, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_pre_connection(pre_connection, pre_ssl, NULL, APR_HOOK_MIDDLE);
	ap_hook_create_request(create_request, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_post_read_request(post_read_request,NULL, asz_succ, APR_HOOK_MIDDLE);
	ap_hook_check_authn(check_authn, NULL,NULL, APR_HOOK_FIRST, AP_AUTH_INTERNAL_PER_CONF);
	ap_hook_access_checker(access_checker, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_check_access(check_access, NULL, NULL, APR_HOOK_MIDDLE, AP_AUTH_INTERNAL_PER_CONF);
	ap_hook_auth_checker(auth_checker, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_fixups(fixups, NULL, NULL, APR_HOOK_LAST);

	APR_OPTIONAL_HOOK(ssl, init_server, init_server, NULL, NULL, APR_HOOK_MIDDLE);
	APR_OPTIONAL_HOOK(ssl, pre_handshake, pre_handshake, NULL, NULL, APR_HOOK_MIDDLE);
	APR_OPTIONAL_HOOK(ssl, proxy_post_handshake, proxy_post_handshake, NULL, NULL, APR_HOOK_MIDDLE);

	ap_register_provider(p, AP_SOCACHE_PROVIDER_GROUP, "openaaa",
	                     AP_SOCACHE_PROVIDER_VERSION, &socache_tls_aaa);

};

module AP_MODULE_DECLARE_DATA __attribute__((visibility("default"))) 
MODULE_ENTRY = {
	STANDARD20_MODULE_STUFF,
	config_init_dir,         /* dir config constructor */
	NULL,                    /* dir merger - default is to override */
	config_init_srv,         /* server config constructor */         
	NULL,                    /* merge server config */
	cmds,                    /* command table */            
	register_hooks,          /* Apache2 register hooks */           
};
