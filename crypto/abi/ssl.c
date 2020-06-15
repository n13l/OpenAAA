/*
 * The MIT License (MIT)                            OpenSSL Runtime ABI Support
 *                               Copyright (c) 2015 Daniel Kubec <niel@rtfm.cz> 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"),to deal 
 * in the Software without restriction, including without limitation the rights 
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <sys/compiler.h>
#include <sys/log.h>

#include <bb.h>
#include <list.h>
#include <dict.h>

#include <mem/pool.h>
#include <mem/stack.h>

#include <dlfcn.h>
#include <unistd.h>
#include <sys/plt/plthook.h>
#include <net/tls/ext.h>

#include <crypto/hex.h>
#include <crypto/b64.h>
#include <crypto/sha1.h>
#include <crypto/abi/lib.h>

#include <unix/timespec.h>

#include <aaa/lib.h>
#include <aaa/prv.h>

#ifdef CONFIG_WIN32
#include <windows.h>                                                            
#include <wincrypt.h>
#endif

#ifdef CONFIG_LINUX
#include <link.h>
#endif

#ifdef CONFIG_ARM
#include <link.h>
#endif

/* We dont't link agaist openssl but using important signatures */
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>

#include <crypto/abi/ssl.h>

#define SSL_USER_IDX 666
#define SSL_SESS_SET(ssl, data) \
	CALL_ABI(SSL_set_ex_data)(ssl, SSL_USER_IDX, data)
#define SSL_SESS_GET(ssl) \
	(struct session *)CALL_ABI(SSL_get_ex_data)(ssl, SSL_USER_IDX)

static const char *aaa_attr_names[] = {
	[AAA_ATTR_AUTHORITY] = "aaa.authority",
	[AAA_ATTR_PROTOCOL]  = "aaa.protocol",
	[AAA_ATTR_VERSION]   = "aaa.version"
};
	
DEFINE_LIST(openssl_symtab);

DEFINE_ABI(SSLeay);
DEFINE_ABI(SSL_CTX_new);
DEFINE_ABI(SSL_CTX_free);
DEFINE_ABI(SSL_CTX_ctrl);
DEFINE_ABI(SSL_CTX_callback_ctrl);
DEFINE_ABI(SSL_CTX_set_ex_data);
DEFINE_ABI(SSL_CTX_get_ex_data);
DEFINE_ABI(SSL_CTX_add_client_custom_ext);
DEFINE_ABI(SSL_CTX_add_server_custom_ext);
DEFINE_ABI(SSL_CTX_set_alpn_protos);
DEFINE_ABI(SSL_new);
DEFINE_ABI(SSL_free);
DEFINE_ABI(SSL_get0_alpn_selected);
DEFINE_ABI(SSL_get_info_callback);
DEFINE_ABI(SSL_get_rfd);
DEFINE_ABI(SSL_get_wfd);
DEFINE_ABI(SSL_ctrl);
DEFINE_ABI(SSL_callback_ctrl);
DEFINE_ABI(SSL_set_ex_data);
DEFINE_ABI(SSL_get_ex_data);
DEFINE_ABI(SSL_CTX_set_info_callback);
DEFINE_ABI(SSL_CTX_get_info_callback);
DEFINE_ABI(SSL_set_info_callback);
DEFINE_ABI(SSL_export_keying_material);
DEFINE_ABI(SSL_state_string);
DEFINE_ABI(SSL_state_string_long);
DEFINE_ABI(SSL_alert_type_string);
DEFINE_ABI(SSL_alert_type_string_long);
DEFINE_ABI(SSL_alert_desc_string);
DEFINE_ABI(SSL_alert_desc_string_long);
DEFINE_ABI(SSL_get_error);
DEFINE_ABI(SSL_get_session);
DEFINE_ABI(SSL_SESSION_free);
DEFINE_ABI(SSL_SESSION_get_id);
DEFINE_ABI(SSL_SESSION_print);
DEFINE_ABI(BIO_new);
DEFINE_ABI(BIO_free);
DEFINE_ABI(BIO_s_mem);
DEFINE_ABI(BIO_ctrl);
DEFINE_ABI(BIO_read);
DEFINE_ABI(X509_NAME_oneline);
DEFINE_ABI(X509_get_subject_name);
DEFINE_ABI(X509_get_issuer_name);
DEFINE_ABI(SSL_get_ex_data_X509_STORE_CTX_idx);
DEFINE_ABI(X509_STORE_CTX_get_ex_data);
DEFINE_ABI(SSL_get_peer_certificate);
DEFINE_ABI(SSL_get_certificate);
DEFINE_ABI(SSL_get_SSL_CTX);
DEFINE_ABI(SSL_CTX_get_cert_store);
DEFINE_ABI(CRYPTO_free);
DEFINE_ABI(SSL_SESSION_set_timeout);
DEFINE_ABI(SSL_SESSION_get_timeout);
DEFINE_ABI(SSL_set_verify_result);
DEFINE_ABI(SSL_shutdown);

struct cf_tls_rfc5705 {
	char *context;
	char *label;
	unsigned int length;
};

struct cf_tls_rfc5705 cf_tls_rfc5705 = {
	.context = "OpenAAA",
	.label   = "EXPORTER_AAA",
	.length  = 16
};

typedef void
(*ssl_cb_ext)(const SSL *ssl, int c, int type, byte *data, int len, void *arg);
typedef void
(*ssl_cb_info)(const SSL *s, int where, int ret);

enum ssl_endpoint_type {
	TLS_EP_PEER   = 0,
	TLS_EP_CLIENT = 1,
	TLS_EP_SERVER = 2	
};

struct ssl_cb {
	ssl_cb_info cb_info;
	ssl_cb_ext cb_ext;
} ssl_cb;

struct list ssl_module_list;

struct ssl_module {
	struct node node;
	char *file;
	void *dll;
};

struct ssl_aaa {
	char *authority;
	char *protocol;
	char *handler;
	char *group;
	char *role;
	int verbose;
};

struct ssl_aaa aaa;

struct aaa_keys {
	struct bb binding_key;
	struct bb binding_id;
};

struct session {
	struct mm_pool *mp;
	struct dict recved;
	struct dict posted;
	struct aaa_keys keys;
	SSL_CTX *ctx;
	SSL *ssl;
	X509 *cert;
	char *tls_binding_key;
	char *aaa_binding_key;
	enum ssl_endpoint_type endpoint;
};

void *libssl = NULL;
void *libcrypto = NULL;
static int server_handshake_synch = 1;
static int server_always          = 0;

static int ssl_sca_enabled = 1;

void
ssl_info(const SSL *s, int where, int ret);

static void
ssl_handshake0(const SSL *ssl);

void
ssl_version(char *str, int size)
{
	long version = CALL_ABI(SSLeay)();

	byte major = (version >> 28) & 0xFF;
	byte minor = (version >> 20) & 0xFF;
	byte patch = (version >> 12) & 0XFF;
	byte dev   = (version >>  4) & 0XFF;

	snprintf(str, size, "%d.%d.%d%c", major, minor, patch, 'a' + dev - 1);
}

int
ssl_require(int a, int b, int c)
{
	long version = CALL_ABI(SSLeay)();

	byte major = (version >> 28) & 0xFF;
	byte minor = (version >> 20) & 0xFF;
	byte patch = (version >> 12) & 0XFF;

	if (a >= major && b >= minor && c >= patch)
		return 0;
	return 1;
}

static inline const char *
ssl_endpoint_str(int type)
{
	return type == TLS_EP_CLIENT ? "client":
	       type == TLS_EP_SERVER ? "server": "undefined";
}

static void
ssl_extensions(SSL *ssl, int c, int type, byte *data, int len, void *arg)
{ 
	struct session *sp = SSL_SESS_GET(ssl);
	sp->endpoint = c ? TLS_EP_CLIENT : TLS_EP_SERVER;

	debug2("extension name=%s type=%d, len=%d endpoint=%d", 
	       tls_strext(type), type, len, sp->endpoint);

	ssl_cb.cb_ext ? ssl_cb.cb_ext(ssl, c, type, data, len, arg):({});
}

void
ssl_callbacks(const SSL *ssl)
{
	void (*fn)(void) = (void (*)(void))CALL_SSL(get_info_callback)(ssl);
	if (!fn)
		goto done;

	if (fn != (void (*)(void))ssl_info)
		ssl_cb.cb_info = (void (*)(const SSL *, int, int))fn;
	else
		return;

done:
	fn = (void (*)(void))ssl_extensions;
	CALL_SSL(set_info_callback)((SSL *)ssl, ssl_info);
}

static struct session *
session_init(const SSL *ssl)
{
	struct mm_pool *mp = mm_pool_create(CPU_PAGE_SIZE, 0);
	struct session *sp = mm_pool_zalloc(mp, sizeof(*sp));

	dict_init(&sp->recved, mm_pool(mp));
	dict_init(&sp->posted, mm_pool(mp));

	sp->mp = mp;
	sp->ssl = (SSL *)ssl;
	ssl_callbacks(ssl);

	SSL_SESS_SET((SSL *)ssl, sp);
	return sp;
}

static void
session_fini(struct session *sp)
{
	SSL *ssl = sp->ssl;
	SSL_SESS_SET(ssl, NULL);
	mm_pool_destroy(sp->mp);
}

static struct session *
session_get0(const SSL *ssl)
{
	struct session *sp = SSL_SESS_GET((SSL *)ssl);
	return sp ? sp : session_init((SSL *)ssl);
}

static inline int
export_keying_material(struct session *sp)
{
	SSL *s = sp->ssl;

	char *lab = cf_tls_rfc5705.label;
	size_t len = strlen(lab);
	size_t sz = cf_tls_rfc5705.length;

	sp->keys.binding_key.len = 0;
	char *key = sp->keys.binding_key.addr = mm_pool_zalloc(sp->mp, sz + 1);
        if (!CALL_SSL(export_keying_material)(s, key, sz, lab, len, NULL,0,0))
		return 1;

	sp->keys.binding_key.len = sz;
	return 0;
}

static void
ssl_exportkeys(struct session *sp)
{
	char *bind_key, *bind_id, *sess_id;
	struct aaa_keys *a = &sp->keys;

	if (!a->binding_key.len || !a->binding_id.len)
		return;

	SSL_SESSION *sess = CALL_ABI(SSL_get_session)(sp->ssl);
	unsigned int len;
	const byte *id = CALL_ABI(SSL_SESSION_get_id)(sess, &len);
	
	bind_key = evala(memhex, a->binding_key.addr, a->binding_key.len);
	debug3("tls_binding_key=%s", bind_key);
	bind_id = evala(memhex, a->binding_id.addr, a->binding_id.len);
	debug3("tls_binding_id=%s", bind_id);
	sess_id = evala(memhex, (char *)id, len);

	/* tls_session_id is empty for tls tickets for client */
	/* this is hack for no_session_id cases (vpn) */
	if (sess_id && *sess_id)
		debug3("tls_session_id=%s", sess_id);
	else
		sess_id = bind_key;

	if (sp->endpoint == TLS_EP_SERVER || server_always) {
		struct aaa *usr = aaa_new(AAA_ENDPOINT_SERVER, 0);
		aaa_attr_set(usr, "sess.id", sess_id);
		aaa_attr_set(usr, "sess.key",bind_key);
		aaa_bind(usr);
		aaa_free(usr);
	}
}

static int
ssl_derive_keys(struct session *sp)
{
	char *key;
	struct aaa_keys *a = &sp->keys;

	if (export_keying_material(sp))
		return -EINVAL;

	struct sha1 sha1;
	sha1_init(&sha1);

#define OPENAAA_COMPAT 1
#ifdef  OPENAAA_COMPAT
	key = evala(memhex, a->binding_key.addr, a->binding_key.len);
	sha1_update(&sha1, key, strlen(key));
#else
	sha1_update(&sha1, a->binding_key.addr, a->binding_key.len);
#endif
	key = sha1_final(&sha1);

	a->binding_id.addr = mm_pool_alloc(sp->mp, SHA1_SIZE);
	memcpy(a->binding_id.addr, key, SHA1_SIZE / 2);
	a->binding_id.len  = SHA1_SIZE / 2;

	ssl_exportkeys(sp);

	return 0;
}

static inline int
ssl_attr_value(struct session *sp, int type, char *str)
{
	dict_set(&sp->recved, aaa_attr_names[type], str);
	debug3("%s: %s", aaa_attr_names[type], str);
	return 0;
}

static inline int
ssl_parse_attr(struct session *sp, char *line, size_t size)
{
	char *s = strchr(line,'=');
	if (!s || !*s)
		return -EINVAL;

	char *p = line + size;
	char *v = s + 1; *p = *s = 0;

	for (int i = 1; i < array_size(aaa_attr_names); i++) {
		if (strncmp(aaa_attr_names[i], line, strlen(aaa_attr_names[i])))
			continue;
		if (ssl_attr_value(sp, i, v))
			return -EINVAL;
		return 0;
	}

	return -EINVAL;
}

static inline int
ssl_attr_lookup(char *line, size_t size, int state)
{
	for (int i = 1; i < array_size(aaa_attr_names); i++)
		if (!strncmp(aaa_attr_names[i], line, size))
			return i;
	return state;
}

static char *
next_attr(char *line, size_t size)
{
	char *p = line;
	while (size) {
		if (!*p && p == line)
			return NULL;
		if (!*p || *p == '\n')
			return p;
		p++; size--;
	}
	return p;
}

static inline int
ssl_parse_attrs(struct session *sp, char *line, size_t size)
{
	int cursor = 0;
	for(char *p = next_attr(line, size); p; p = next_attr(line, size)) {
		int state = ssl_attr_lookup(line, p - line, cursor);
		if (state == cursor) 
			ssl_parse_attr(sp, line, p - line);

		line = p + 1;
		cursor = state;
	}

	return 0;
}

static int
ssl_server_add(SSL *s, uint type, const byte **out, size_t *len, int *al, void *arg)
{
	struct session *sp = session_get0(s);
	struct mm_pool *mp = sp ? sp->mp : NULL;

	sp->endpoint = TLS_EP_SERVER;

	if (!ssl_sca_enabled)
		return 0;

	dict_set(&sp->posted, "aaa.authority", aaa.authority);
	dict_set(&sp->posted, "aaa.protocol",  aaa.protocol);
	dict_set(&sp->posted, "aaa.version",   "1.0");

	char bb[8192];
	unsigned int sz = 0;
	dict_for_each(attr, sp->posted.list) {
		sz += snprintf(bb, sizeof(bb) - sz - 1, "%s=%s\n",attr->key, attr->val);
	}

	char *b = mm_pool_alloc(mp, sz + 1);
	*len = sz + 1;
	sz = 0;
	dict_for_each(attr, sp->posted.list) {
		debug1("extension %s=%s", attr->key, attr->val);
		sz += snprintf(b + sz, *len, "%s=%s\n",attr->key, attr->val);
	}

	b[sz] = 0;
	debug1("extension name=%s type=%d send",tls_strext(type), type);
	*out = b;
	return 1;
}

int
ssl_server_get(SSL *s, uint type, const byte*in, size_t len, int *l, void *a)
{
	struct session *sp = session_get0(s);
	debug2("extension name=%s type=%d recv", tls_strext(type), type);
	if (len && (type == TLS_EXT_SUPPLEMENTAL_DATA))
		ssl_parse_attrs(sp, (char *)in, len);

	return 1;
}

int
ssl_client_add(SSL *s, unsigned int type, const byte **out, size_t *len, 
               int *al, void *arg)
{
	struct session *sp = session_get0(s);
	struct mm_pool *mp = sp ? sp->mp : NULL;

	sp->endpoint = TLS_EP_CLIENT;
	if (!ssl_sca_enabled)
		return 0;

	dict_set(&sp->recved, "aaa.protocol", aaa.protocol);
	dict_set(&sp->recved, "aaa.version",  "1.0");

	char bb[8192];
	unsigned int sz = 0;
	dict_for_each(a, sp->recved.list) {
		sz += snprintf(bb + sz, sizeof(bb) - sz, "%s=%s\n",a->key, a->val);
	}

	char *b = mm_pool_alloc(mp, sz + 1);
	*len = sz + 1;
	sz = 0;
	dict_for_each(attr, sp->recved.list) {
		debug1("extension %s=%s", attr->key, attr->val);
		sz += snprintf(b + sz, *len, "%s=%s\n",attr->key, attr->val);
	}

	b[sz] = 0;
	debug1("extension name=%s type=%d send ",tls_strext(type), type);

	*out = b;
	return 1;
}

int
ssl_client_get(SSL *ssl, unsigned int type, const byte *in, size_t len, 
                 int *al, void *arg)
{
	struct session *sp = session_get0(ssl);
	debug2("extension name=%s type=%d recv", tls_strext(type), type);

	if (len && (type == TLS_EXT_SUPPLEMENTAL_DATA))
		ssl_parse_attrs(sp, (char *)in, len);

	return 1;
}

/*
static inline void
pubkey_fixups(char *str, unsigned int len)
{
	char *p = str;
	for (unsigned int i = 0; i < len && *p; i++, p++)
		if (*p == '\r' || *p == ' ' || *p == '\t')
			continue;
		else
			*str++ = *p;
	*str = 0;
}

static void x509_fingerprint(void)
{
	int j;                                                          
	unsigned int n;                                                 
	unsigned char md[EVP_MAX_MD_SIZE];                              
	const EVP_MD *fdig = digest;                                    
	if (!fdig)                                                      
	fdig = EVP_sha1();                                          
	if (!X509_digest(x, fdig, md, &n)) {                            
	BIO_printf(bio_err, "out of memory\n");                     
	goto end;                                                   
	}                                                               
	BIO_printf(out, "%s Fingerprint=",                              
	OBJ_nid2sn(EVP_MD_type(fdig)));                      
	for (j = 0; j < (int)n; j++) {                                  
		BIO_printf(out, "%02X%c", md[j], (j + 1 == (int)n)          
		? '\n' : ':');                                   
	}
}

static inline void
pubkey_derive_key(struct session *sp, X509 *x)
{
	byte pub[8192];
        EVP_PKEY *key = sym_X509_get_pubkey(x);
        if (!key)
		return;

	int size = sym_EVP_PKEY_size(key);
	int bits = sym_EVP_PKEY_bits(key);

	BIO *bio = sym_BIO_new(sym_BIO_s_mem());
	sym_PEM_write_bio_PUBKEY(bio, key);

	BUF_MEM *bptr;
	sym_BIO_ctrl(bio, BIO_C_GET_BUF_MEM_PTR, 0,(char *)&bptr);

	if (bptr->length > (sizeof(pub) - 1))
		goto cleanup;

	memcpy(pub, bptr->data, bptr->length);
	pub[bptr->length] = 0;

	pubkey_fixups((char *)pub, bptr->length);
	int hash = hash_string((char *)pub);

        sha1_context sha1;
        sha1_init(&sha1);
        sha1_update(&sha1, (byte *)pub, bptr->length);
        sha1_update(&sha1, (byte *)tls->key, tls->key_size);

        memcpy(tls->sec, (char *)sha1_final(&sha1), SHA1_SIZE);
	tls->sec_size = SHA1_SIZE;
        char *sec = stk_mem_to_hex(tls->sec, SHA1_SIZE);	

	debug("checking for server public key: len=%d size=%d bits=%d hash=%d", 
	        bptr->length, size, bits, hash);

	debug("checking for derived binding key: aaa_binding_key=%s", sec);

cleanup:
	sym_BIO_free(bio);
	sym_EVP_PKEY_free(key);
}
*/

/*
static int
ssl_setsession(struct session *sp)
{
	SSL_SESSION *sess = CALL_ABI(SSL_get_session)(sp->ssl);
	unsigned int len;
	const byte *id = CALL_ABI(SSL_SESSION_get_id)(sess, &len);
	char *key = evala(memhex, (char *)id, len);

	struct aaa *aaa = aaa_new(AAA_ENDPOINT_SERVER, 0);
	aaa_attr_set(aaa, "sess.id", key);
	aaa_bind(aaa, 0, key);

	aaa_free(aaa);

	return 0;
}
*/

static int
ssl_server_aaa(struct session *sp)
{
	struct aaa_keys *a = &sp->keys;
	char *key = evala(memhex, a->binding_key.addr, a->binding_key.len);
	char *id  = evala(memhex, a->binding_id.addr, a->binding_id.len);

	const char *proto_attr   = aaa_attr_names[AAA_ATTR_PROTOCOL];
	const char *proto_client = dict_get(&sp->recved, proto_attr);
	const char *proto_server = aaa.protocol;

	SSL_SESSION *sess = CALL_ABI(SSL_get_session)(sp->ssl);
	unsigned int len;
	const byte *sessid = CALL_ABI(SSL_SESSION_get_id)(sess, &len);
	char *sess_id = evala(memhex, (char *)sessid, len);

	long timeout = CALL_ABI(SSL_SESSION_get_timeout)(sess);
	info("ssl.timeout: %ld", timeout);

	struct aaa *usr = aaa_new(AAA_ENDPOINT_SERVER, 0);

	if (!sess_id || !*sess_id)
		sess_id = key;

	info("aaa.authority=%s", aaa.authority);
	info("aaa.handler=%s", aaa.handler);

	info("protocol server=%s client=%s", proto_server, proto_client);

	aaa_attr_set(usr, "sess.id", sess_id);
	aaa_attr_set(usr, "sess.key", key);
	aaa_bind(usr);
	aaa_free(usr);

	if (!proto_client || !proto_server)
		return -EINVAL;
	if (strcmp(proto_client, proto_server))
		return -EINVAL;
	if (!aaa.handler || !key || !id || !aaa.authority)
		return -EINVAL;

//	ssl_setsession(sp);
//
	debug2("handshake_synch=%s",  server_handshake_synch ? "yes": "no");

	char *synch = "";
#ifdef CONFIG_LINUX	
	synch = server_handshake_synch ? "" : "&";
#endif
	char *host = aaa.authority;
	char *msg;
	if (aaa.group && aaa.role)
		msg = printfa("%s -pri -a%s -i%s -k%s -g%s -r%s", 
		         aaa.handler, host, id, key, aaa.group, aaa.role);
	else
		msg = printfa("%s -pri -a%s -i%s -k%s", 
		              aaa.handler, host, id, key);
	
	_unused int status = system(msg);
	debug1("%s", WEXITSTATUS(status)? "failed" : "channel binding");

	if (aaa.group && aaa.role)
		msg = printfa("%s -pr4 -s%s -a%s -i%s -k%s -g%s -r%s %s", 
		         aaa.handler, sess_id, host, id, key, aaa.group, aaa.role, synch);
	else
		msg = printfa("%s -pr4 -s%s -a%s -i%s -k%s %s", 
		              aaa.handler, sess_id, host, id, key, synch);

	debug1("cmd=%s", msg);
	
	status = system(msg);

	if (!server_handshake_synch)
		debug1("%s", WEXITSTATUS(status)? "forbidden" : "authenticated");

	if (!server_handshake_synch)
		return 0;

	CALL_SSL(set_verify_result)(sp->ssl, X509_V_ERR_APPLICATION_VERIFICATION);
	CALL_SSL(shutdown)(sp->ssl);
	return X509_V_ERR_APPLICATION_VERIFICATION;
}

static int
ssl_client_aaa(struct session *sp)
{
	const char *authority = dict_get(&sp->recved, "aaa.authority");

	struct aaa_keys *a = &sp->keys;
	char *key = evala(memhex, a->binding_key.addr, a->binding_key.len);
	char *id  = evala(memhex, a->binding_id.addr,  a->binding_id.len);

	authority = authority ? authority : aaa.authority;
	debug4("authority=%s", authority);
	debug4("handler=%s", aaa.handler);

	if (!aaa.handler || !key || !id || !authority)
		return -EINVAL;

#ifdef CONFIG_WIN32
	const char *pre = "START /B ", *end = "";
	const char *msg = printfa("%s %s -k%s -i%s -prx -a%s %s", 
	                          pre, aaa.handler, key, id, authority, end);

#else
	const char *pre = "", *end = "&";
	const char *msg = printfa("%s%s -k%s -i%s -prx -a%s %s", 
	                          pre, aaa.handler, key, id, authority, end);
#endif
	_unused int status = system(msg);
	return WEXITSTATUS(status);
}

static void
ssl_handshake0(const SSL *ssl)
{

}

/* TLS Handshake phaze 1 */
static void
ssl_handshake1(const SSL *ssl)
{
	struct session *sp = SSL_SESS_GET(ssl);
	const char *endpoint = ssl_endpoint_str(sp->endpoint);
	X509_NAME *x_subject, *x_issuer;
	char *subject = NULL, *issuer = NULL;

	if (sp->endpoint == TLS_EP_SERVER) 
		sp->cert = CALL_SSL(get_certificate)((SSL *)ssl);
	else if (sp->endpoint == TLS_EP_CLIENT)
		sp->cert = CALL_SSL(get_peer_certificate)((SSL *)ssl);
	else goto cleanup;

	debug2("%s checking for server certificate: %s", 
	      endpoint, sp->cert ? "Yes" : "No");
	if (!sp->cert)
		goto cleanup;

	x_subject = CALL_ABI(X509_get_subject_name)(sp->cert);
	x_issuer  = CALL_ABI(X509_get_issuer_name)(sp->cert);
	subject   = CALL_ABI(X509_NAME_oneline)(x_subject, NULL, 0);
	issuer    = CALL_ABI(X509_NAME_oneline)(x_issuer,  NULL, 0);

	debug2("checking for subject: %s", subject);
	debug2("checking for issuer:  %s", issuer);

	if (!ssl_sca_enabled)
		return;

	ssl_derive_keys(sp);

	const unsigned char *alpn = NULL;
	unsigned int size = 0;
	CALL_SSL(get0_alpn_selected)(ssl, &alpn, &size);

	if (sp->endpoint)
		debug2("%s checking for application-layer protocol negotiation: %s",
		       endpoint, size ? strmema(alpn, size) : "No");

	if (sp->endpoint == TLS_EP_SERVER) 
		ssl_server_aaa(sp);
	else if (sp->endpoint == TLS_EP_CLIENT)
		ssl_client_aaa(sp);

cleanup:
	if (subject)
		CALL_ABI(CRYPTO_free)(subject, __FILE__, __LINE__);
	if (issuer)
		CALL_ABI(CRYPTO_free)(issuer, __FILE__, __LINE__);

	session_fini(sp);
}

const char *
ssl_get_value_desc(const SSL *s, int code)
{
	return NULL;
}

static void
ssl_info_state(const SSL *s, const char *str)
{
        if (!EXISTS_ABI(SSL_state_string_long))
                return;

	const char *state = CALL_SSL(state_string_long)(s);
	char *d = printfa("%s:%s", str, state);
	debug2("msg:%s", d);
}

static void
ssl_info_alert(int where, int rv)
{
        if (!EXISTS_ABI(SSL_alert_type_string_long))
                return;
        if (!EXISTS_ABI(SSL_alert_desc_string_long))
                return;

	const char *type = CALL_SSL(alert_type_string_long)(rv);
	const char *desc = CALL_SSL(alert_desc_string_long)(rv);

	char *v = printfa("alert %s:%s:%s", (where & SSL_CB_READ) ?
	                    "read" : "write", type, desc);
	debug2("msg:%s", v);
}

static void
ssl_info_failed(const SSL *s, const char *str, int rv)
{
	char *err = printfa("%s:failed in %s", str, CALL_SSL(state_string_long)(s));
	const char *desc = ssl_get_value_desc(s, rv);
	debug2("msg:%s %s", err, desc);
}

static void
ssl_info_default(const SSL *s, const char *str, int rv)
{
	char *e = printfa("%s:error in %s", str, CALL_SSL(state_string_long)(s));
	const char *desc = ssl_get_value_desc(s, rv);
	debug2("msg:%s %s", e, desc);
}

static void
ssl_info_error(const SSL *s, const char *str, int rv)
{
	switch(CALL_ABI(SSL_get_error)(s, rv)) {
	case SSL_ERROR_WANT_READ:
		break;
	case SSL_ERROR_WANT_WRITE:
		break;
	default:
		ssl_info_default(s, str, rv);
		break;
	}
}

static inline const char *
ssl_state_str(int w)
{
	return (w & SSL_ST_CONNECT)     ? "connect" :
	       (w & SSL_ST_ACCEPT)      ? "accept"  :
//	       (w & SSL_ST_INIT)        ? "init" :
//	       (w & SSL_ST_BEFORE)      ? "before" :
//	       (w & SSL_ST_OK)          ? "ok" :
//	       (w & SSL_ST_RENEGOTIATE) ? "renegotiate" : "negotiate": "";
	       "";
}

void
ssl_info(const SSL *s, int where, int ret)
{
	struct session *sp = session_get0(s);

        int w = where & ~SSL_ST_MASK, rv = ret;
	const char *str = ssl_state_str(w);

	if (where & SSL_CB_HANDSHAKE_DONE) {
		ssl_info_state(s, str);
	}
        if (where & SSL_CB_LOOP) {
		ssl_info_state(s, str);
	} else if (where & SSL_CB_ALERT) {
		ssl_info_alert(where, rv);
	} else if (where & SSL_CB_EXIT) {
		if (rv == 0) {
			ssl_info_failed(s, str, rv);
		} else if (rv < 0)
			ssl_info_error(s, str, rv);
	}

	if ((where & SSL_CB_HANDSHAKE_START) && sp)
		ssl_handshake0(s);
	if (where & SSL_CB_HANDSHAKE_DONE)
		ssl_handshake1(s);

	if (ssl_cb.cb_info)
		ssl_cb.cb_info(s, where, ret);
}

void
DEFINE_CTX_CALL(set_info_callback)(SSL_CTX *ctx, ssl_cb_info cb)
{
	ssl_cb.cb_info = cb;
	debug4("ctx=%p", ctx);
}

void
DEFINE_SSL_CALL(set_info_callback)(SSL *ssl, ssl_cb_info cb)
{
	ssl_cb.cb_info = cb;
	debug4("ssl=%p", ssl);
}

long
DEFINE_SSL_CALL(callback_ctrl)(SSL *ssl, int cmd, void (*fp)(void))
{
	debug4("ssl=%p", ssl);
	switch (cmd) {
	case SSL_CTRL_SET_TLSEXT_DEBUG_CB:
		ssl_cb.cb_ext    = (typeof(ssl_cb.cb_ext))fp;
		void (*fn)(void) = (void (*)(void))ssl_extensions;
		return CALL_SSL(callback_ctrl)(ssl, cmd, fn);
	default:
		debug1("cmd=%d", cmd);
		return CALL_SSL(callback_ctrl)(ssl, cmd, fp);
	}
	return 0;
}

long
DEFINE_CTX_CALL(callback_ctrl)(SSL_CTX *ctx, int cmd, void (*fp)(void))
{
	debug4("ctx=%p", ctx);

	switch (cmd) {
	case SSL_CTRL_SET_TLSEXT_DEBUG_CB:
		debug4("SSL_CTRL_SET_TLSEXT_DEBUG_CB");
		ssl_cb.cb_ext = (typeof(ssl_cb.cb_ext))fp;
		break;
	default:
		debug1("cmd=%d", cmd);
		return CALL_CTX(callback_ctrl)(ctx, cmd, fp);
	}
	return 0;
}

int
DEFINE_CTX_CALL(set_ex_data)(SSL_CTX *ctx, int index, void *data)
{
	debug4("ctx=%p index=%d data=%p", ctx, index, data);
	return CALL_CTX(set_ex_data)(ctx, index, data);
}

void *
DEFINE_CTX_CALL(get_ex_data)(const SSL_CTX *ctx, int index)
{
	debug4("ctx=%p index=%d", ctx, index);
	return CALL_CTX(get_ex_data)(ctx, index);
}

int
DEFINE_SSL_CALL(set_ex_data)(SSL *ssl, int index, void *data)
{
	debug4("ssl=%p index=%d data=%p", ssl, index, data);
	return CALL_SSL(set_ex_data)(ssl, index, data);
}

void *
DEFINE_SSL_CALL(get_ex_data)(const SSL *ssl, int index)
{
	debug4("ssl=%p index=%d", ssl, index);
	return CALL_SSL(get_ex_data)(ssl, index);
}

SSL_CTX *
DEFINE_CTX_CALL(new)(const SSL_METHOD *method)
{
	SSL_CTX *ctx = CALL_ABI(SSL_CTX_new)(method);

	void (*fn)(void) = (void (*)(void))ssl_extensions;
	CALL_CTX(set_info_callback)(ctx, ssl_info);
	CALL_CTX(callback_ctrl)(ctx, SSL_CTRL_SET_TLSEXT_DEBUG_CB, fn);

	CALL_CTX(add_client_custom_ext)(ctx, 1000, ssl_client_add, NULL, NULL,
	                                ssl_client_get, NULL);
	CALL_CTX(add_server_custom_ext)(ctx, 1000, ssl_server_add, NULL, NULL, 
	                                ssl_server_get, NULL);
	debug4("ctx=%p", ctx);	
	return ctx;
}

SSL *
DEFINE_SSL_CALL(new)(SSL_CTX *ctx)
{
	SSL *ssl = CALL_SSL(new)(ctx);

	void (*fn)(void) = (void (*)(void))ssl_extensions;
	CALL_SSL(set_info_callback)(ssl, ssl_info);
	CALL_SSL(callback_ctrl)(ssl, SSL_CTRL_SET_TLSEXT_DEBUG_CB, fn);
	
	debug3("ssl=%p", ssl);
	return ssl;
}

void
DEFINE_CTX_CALL(set_alpn_protos)(SSL_CTX *ctx, const u8 *data, unsigned int len)
{
	debug4("len=%u", len);
}

void
symbol_print(void)
{
	list_for_each(openssl_symtab, p, struct symbol, node) {
		debug4("name=%s abi=%p plt=%p", p->name, p->abi, p->plt);
		if (!p->abi)
			die("required symbol not found");
	}
}

int
lookup_module(struct dl_phdr_info *info, size_t size, void *ctx)
{
	if (!info->dlpi_name || !*info->dlpi_name)
		return 0;

	int ssl = 0;
	if (strstr(info->dlpi_name, "tcnative"))
		ssl = 1;
	if (strstr(info->dlpi_name, "mod_ssl"))
		ssl = 1;

	void *dll = dlopen(info->dlpi_name, RTLD_LAZY);
	void *sym = dll ? dlsym(dll, "SSL_CTX_new") : NULL;
	if (sym) libssl = dll;
	sym = (dll && !sym) ? dlsym(dll, "SSLeay") : sym;
	if (sym) libcrypto = dll;

	char *v = ssl ? "framework" : "cryptolib";
	debug4("module type=%-9s name=%s", v, info->dlpi_name);

	if (!ssl && !sym)
		return 0;
/*
	v = ssl ? "framework" : "cryptolib";
	debug4("module type=%-9s name=%s", v, info->dlpi_name);
*/
	struct ssl_module *ssl_module = malloc(sizeof(*ssl_module));
	ssl_module->dll = dll;
	ssl_module->file = strdup(info->dlpi_name);
	
	list_add(&ssl_module_list, &ssl_module->node);
	if (!ssl)
		return 0;

#ifdef CONFIG_WIN32
	dll = dlopen(info->dlpi_name, RTLD_GLOBAL);
#endif
	snprintf(ctx, 254, "%s", info->dlpi_name);
	return 0;
}

static void
find_module(char *ssl_module)
{
	dl_iterate_phdr(lookup_module, ssl_module);
}

static void
import_target(void *dll)
{
	int rv;
	plthook_t *plt = NULL;
	if (!dll)
		rv = plthook_open(&plt, NULL);
	else
		rv = plthook_open_by_handle(&plt, dll);

	if (rv)
		error("%s", plthook_error());

	if (!plt || rv)
		return;

	debug4("module imported %s", dll ? "framework" : "target");
	UPDATE_ABI(SSL_CTX_callback_ctrl);
	UPDATE_ABI(SSL_CTX_set_info_callback);
	UPDATE_ABI(SSL_CTX_new);
	UPDATE_ABI(SSL_callback_ctrl);
	UPDATE_ABI(SSL_set_info_callback);
	UPDATE_ABI(SSL_new);
	plthook_close(plt);
}

static void
init_aaa_env(void)
{
	memset(&aaa, 0, sizeof(aaa));

	char *authority = getenv("OPENAAA_AUTHORITY");
	aaa.authority = authority ? authority : NULL;
	char *protocol = getenv("OPENAAA_PROTOCOL");
	aaa.protocol = protocol ? protocol : NULL;
	char *handler = getenv("OPENAAA_HANDLER");
	aaa.handler = handler ? handler : NULL;
	char *group = getenv("OPENAAA_GROUP");
	aaa.group = group ? group: NULL;
	char *role = getenv("OPENAAA_ROLE");
	aaa.role = role ? role : NULL;

	char *verb = getenv("OPENAAA_VERBOSE");
	if (verb)
		log_verbose = atoi(verb);

	//debug("checking for aaa environment");
	if (aaa.authority)
		debug1("env aaa.authority=%s",aaa.authority);
	if (aaa.protocol)
		debug1("env aaa.protocol=%s",aaa.protocol);
	if (aaa.handler)
		debug1("env aaa.handler=%s",aaa.handler);
	if (aaa.group)
		debug1("env aaa.group=%s",aaa.group);
	if (aaa.role)
		debug1("env aaa.role=%s",aaa.role);

}

static int is_ssl_init = 0;

int
ssl_init(int server)
{
	if (is_ssl_init)
		return -1;

	is_ssl_init = 1;
	server_handshake_synch = 0;
	server_always = server;

	list_init(&ssl_module_list);

	IMPORT_ABI(SSLeay);
	IMPORT_ABI(SSL_CTX_new);
	IMPORT_ABI(SSL_CTX_free);
	IMPORT_ABI(SSL_CTX_ctrl);
	IMPORT_ABI(SSL_CTX_callback_ctrl);
	IMPORT_ABI(SSL_CTX_set_ex_data);
	IMPORT_ABI(SSL_CTX_get_ex_data);
	IMPORT_ABI(SSL_CTX_add_client_custom_ext);
	IMPORT_ABI(SSL_CTX_add_server_custom_ext);
	IMPORT_ABI(SSL_CTX_set_alpn_protos);
	IMPORT_ABI(SSL_new);
	IMPORT_ABI(SSL_get0_alpn_selected);
	IMPORT_ABI(SSL_ctrl);
	IMPORT_ABI(SSL_get_info_callback);
	IMPORT_ABI(SSL_callback_ctrl);
	IMPORT_ABI(SSL_set_ex_data);
	IMPORT_ABI(SSL_get_ex_data);
	IMPORT_ABI(SSL_CTX_set_info_callback);
	IMPORT_ABI(SSL_CTX_get_info_callback);
	IMPORT_ABI(SSL_set_info_callback);
	IMPORT_ABI(SSL_export_keying_material);
	IMPORT_ABI(SSL_state_string);
	IMPORT_ABI(SSL_state_string_long);
	IMPORT_ABI(SSL_alert_type_string);
	IMPORT_ABI(SSL_alert_type_string_long);
	IMPORT_ABI(SSL_alert_desc_string);
	IMPORT_ABI(SSL_alert_desc_string_long);
	IMPORT_ABI(SSL_get_error);
	IMPORT_ABI(SSL_get_session);
	IMPORT_ABI(SSL_SESSION_free);
	IMPORT_ABI(SSL_SESSION_get_id);
	IMPORT_ABI(SSL_SESSION_print);
	IMPORT_ABI(BIO_new);
	IMPORT_ABI(BIO_free);
	IMPORT_ABI(BIO_s_mem);
	IMPORT_ABI(BIO_ctrl);
	IMPORT_ABI(BIO_read);
	IMPORT_ABI(X509_NAME_oneline);
	IMPORT_ABI(X509_get_subject_name);
	IMPORT_ABI(X509_get_issuer_name);
	IMPORT_ABI(SSL_get_ex_data_X509_STORE_CTX_idx);
	IMPORT_ABI(X509_STORE_CTX_get_ex_data);
	IMPORT_ABI(SSL_get_peer_certificate);
	IMPORT_ABI(SSL_get_certificate);
	IMPORT_ABI(SSL_get_SSL_CTX);
	IMPORT_ABI(SSL_CTX_get_cert_store);
	IMPORT_ABI(CRYPTO_free);
	IMPORT_ABI(SSL_SESSION_set_timeout);
	IMPORT_ABI(SSL_SESSION_get_timeout);
	IMPORT_ABI(SSL_set_verify_result);
	IMPORT_ABI(SSL_shutdown);

	init_aaa_env();
	aaa_env_init();

	return 0;
}

void
ssl_init_ctxt(SSL_CTX *ctx)
{
	void (*fn)(void) = (void (*)(void))ssl_extensions;
	CALL_CTX(callback_ctrl)(ctx, SSL_CTRL_SET_TLSEXT_DEBUG_CB, fn);
	CALL_CTX(add_client_custom_ext)(ctx, 1000, ssl_client_add, NULL, NULL,
	                                ssl_client_get, NULL);
	CALL_CTX(add_server_custom_ext)(ctx, 1000, ssl_server_add, NULL, NULL, 
	                                ssl_server_get, NULL);
}

void
ssl_init_conn(SSL *ssl)
{
	_unused struct session *sp = session_get0(ssl);
}

#define CHECK_ERR(cond) if (cond) return -1;

int
ssl_get_sess_id(SSL *ssl, char *buf, int size)
{
	memset(buf, 0, size);
	
	SSL_SESSION *sess = CALL_ABI(SSL_get_session)(ssl);
	CHECK_ERR(!sess);

	unsigned int len = 32;
	const byte *sessid = CALL_ABI(SSL_SESSION_get_id)(sess, &len);
	CHECK_ERR(!sessid || len != 32);

	char *sess_id = evala(memhex, (char *)sessid, len);
	memcpy(buf, sess_id, strlen(sess_id));

	return 0;
}

void
ssl_set_caps(int cap)
{
	if ((cap & SSL_CAP_SCA))
		ssl_sca_enabled = 1; else ssl_sca_enabled = 0;

}

int
crypto_lookup(void)
{
	init_aaa_env();

	list_init(&ssl_module_list);

	char ssl_module[255] = {0};
	find_module(ssl_module);

	debug4("module %s", *ssl_module ? "framework" : "target");
	void *dll = *ssl_module ? dlopen(ssl_module, RTLD_LAZY | RTLD_NOLOAD): NULL;

	IMPORT_ABI(SSLeay);
	IMPORT_ABI(SSL_CTX_new);
	IMPORT_ABI(SSL_CTX_free);
	IMPORT_ABI(SSL_CTX_ctrl);
	IMPORT_ABI(SSL_CTX_callback_ctrl);
	IMPORT_ABI(SSL_CTX_set_ex_data);
	IMPORT_ABI(SSL_CTX_get_ex_data);
	IMPORT_ABI(SSL_CTX_add_client_custom_ext);
	IMPORT_ABI(SSL_CTX_add_server_custom_ext);
	IMPORT_ABI(SSL_CTX_set_alpn_protos);
	IMPORT_ABI(SSL_new);
	IMPORT_ABI(SSL_get0_alpn_selected);
	IMPORT_ABI(SSL_ctrl);
	IMPORT_ABI(SSL_get_info_callback);
	IMPORT_ABI(SSL_callback_ctrl);
	IMPORT_ABI(SSL_set_ex_data);
	IMPORT_ABI(SSL_get_ex_data);
	IMPORT_ABI(SSL_CTX_set_info_callback);
	IMPORT_ABI(SSL_CTX_get_info_callback);
	IMPORT_ABI(SSL_set_info_callback);
	IMPORT_ABI(SSL_export_keying_material);
	IMPORT_ABI(SSL_state_string);
	IMPORT_ABI(SSL_state_string_long);
	IMPORT_ABI(SSL_alert_type_string);
	IMPORT_ABI(SSL_alert_type_string_long);
	IMPORT_ABI(SSL_alert_desc_string);
	IMPORT_ABI(SSL_alert_desc_string_long);
	IMPORT_ABI(SSL_get_error);
	IMPORT_ABI(SSL_get_session);
	IMPORT_ABI(SSL_SESSION_free);
	IMPORT_ABI(SSL_SESSION_get_id);
	IMPORT_ABI(SSL_SESSION_print);
	IMPORT_ABI(BIO_new);
	IMPORT_ABI(BIO_free);
	IMPORT_ABI(BIO_s_mem);
	IMPORT_ABI(BIO_ctrl);
	IMPORT_ABI(BIO_read);
	IMPORT_ABI(X509_NAME_oneline);
	IMPORT_ABI(X509_get_subject_name);
	IMPORT_ABI(X509_get_issuer_name);
	IMPORT_ABI(SSL_get_ex_data_X509_STORE_CTX_idx);
	IMPORT_ABI(X509_STORE_CTX_get_ex_data);
	IMPORT_ABI(SSL_get_peer_certificate);
	IMPORT_ABI(SSL_get_certificate);
	IMPORT_ABI(SSL_get_SSL_CTX);
	IMPORT_ABI(SSL_CTX_get_cert_store);
	IMPORT_ABI(CRYPTO_free);
	IMPORT_ABI(SSL_SESSION_set_timeout);
	IMPORT_ABI(SSL_SESSION_get_timeout);
	IMPORT_ABI(SSL_set_verify_result);
	IMPORT_ABI(SSL_shutdown);

	import_target(dll);

	init_aaa_env();

	aaa_env_init();
	return 0;
}

void
crypto_handshake_asynch(int yes)
{
	server_handshake_synch = yes ? 0 : 1;
}


