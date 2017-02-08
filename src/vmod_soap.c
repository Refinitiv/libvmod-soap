/*
 * Copyright 2017 Thomson Reuters
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <syslog.h>
#include "vmod_soap.h"

#define POOL_KEY "VRN_IH_PK"

static pthread_mutex_t	soap_mutex = PTHREAD_MUTEX_INITIALIZER;
static int		refcount = 0;
static apr_pool_t	*apr_pool = NULL;

enum soap_state {
	NONE = 0,
	INIT,
	HEADER,
	BODY,
	DONE
};

/* -------------------------------------------------------------------------------------/
   init module
*/
static void init_apr()
{
	AZ(apr_pool);
	XXXAZ(apr_initialize());
	XXXAZ(apr_pool_create(&apr_pool, NULL));
}

static void clean_apr()
{
	apr_pool = NULL;
	apr_terminate();
}

/* -------------------------------------------------------------------------------------/
   init vcl
*/
static void clean_vcl(void *priv)
{
	struct priv_soap_vcl *priv_soap_vcl;
	struct soap_namespace *ns, *ns2;

	CAST_OBJ_NOTNULL(priv_soap_vcl, priv, PRIV_SOAP_VCL_MAGIC);

	VSLIST_FOREACH_SAFE(ns, &priv_soap_vcl->namespaces, list, ns2) {
		VSLIST_REMOVE_HEAD(&priv_soap_vcl->namespaces, list);
		FREE_OBJ(ns);
	}

	FREE_OBJ(priv_soap_vcl);
}

static struct priv_soap_vcl* init_vcl()
{
	struct priv_soap_vcl *priv_soap_vcl;

	ALLOC_OBJ(priv_soap_vcl, PRIV_SOAP_VCL_MAGIC);
	XXXAN(priv_soap_vcl);

	VSLIST_INIT(&priv_soap_vcl->namespaces);
	return priv_soap_vcl;
}

/* ------------------------------------------------------------------/
   initialize session
*/
static struct priv_soap_task* init_task(VRT_CTX)
{
	struct priv_soap_task *priv_soap_task;

	ALLOC_OBJ(priv_soap_task, PRIV_SOAP_TASK_MAGIC);
	AN(priv_soap_task);

	priv_soap_task->ctx = ctx;

	XXXAZ(apr_pool_create(&priv_soap_task->pool, apr_pool));

	ALLOC_OBJ(priv_soap_task->req_http, SOAP_REQ_HTTP_MAGIC);
	XXXAN(priv_soap_task->req_http);

	ALLOC_OBJ(priv_soap_task->req_xml, SOAP_REQ_XML_MAGIC);
	XXXAN(priv_soap_task->req_xml);
	return priv_soap_task;
}

/* -----------------------------------------------------------------/
   destroy session
*/
static void clean_task(void *priv)
{
	struct priv_soap_task *priv_soap_task;

	AN(priv);
	CAST_OBJ_NOTNULL(priv_soap_task, priv, PRIV_SOAP_TASK_MAGIC);

	clean_req_xml(priv_soap_task->req_xml);
	priv_soap_task->req_xml = NULL;

	AN(priv_soap_task->pool);
	apr_pool_destroy(priv_soap_task->pool);

	FREE_OBJ(priv_soap_task);
}

int process_request(struct priv_soap_task *task, enum soap_state state)
{
	while (task->state < state) {
		switch (task->state) {
		case INIT:  // init
			task->req_http->pool = task->pool;
			task->req_http->ctx = task->ctx;
			init_req_http(task->req_http);

			init_gzip(task->req_http);

			task->req_xml->pool = task->pool;
			task->req_xml->ctx = task->ctx;
			init_req_xml(task->req_xml);

			task->bytes_left = task->req_http->cl;
			task->state = HEADER;
			break;
		case HEADER:  // want header
		case BODY:  // want body
			while (task->bytes_left > 0) {
				// read http body & uncompress it
				body_part uncompressed_body_part;
				int bytes_read = read_body_part(task->req_http, task->bytes_left, &uncompressed_body_part);
				if (bytes_read <= 0) {
					//TODO:add_soap_error(r, 500, "Error reading soap, err: %d", errno );
					//TODO:goto E_x_i_t;
				}
				task->bytes_left -= bytes_read;

				// parse chunk
				parse_soap_chunk(task->req_xml, uncompressed_body_part.data, uncompressed_body_part.length);

				if (task->req_xml->body) {
					task->state = DONE;
					break;
				}
				else if (task->req_xml->header && task->req_xml->action_namespace && task->req_xml->action_name) {
					task->state = BODY;
					break;
				}
			}
			break;
		case DONE:  // read from memory
			break;
		default:
			break;
		}
	}
	return 0;
}

/*
 * handle vmod internal state, vmod init/fini and/or varnish callback
 * (un)registration here.
 *
 */
int __match_proto__(vmod_event_f)
	event_function(VRT_CTX, struct vmod_priv *priv /* PRIV_VCL */, enum vcl_event_e e)
{
	struct priv_soap_vcl *priv_soap_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	switch (e) {
	case VCL_EVENT_LOAD:
		AZ(pthread_mutex_lock(&soap_mutex));
		if(0 == refcount++) {
			init_xml();
			init_apr();
		}
		AZ(pthread_mutex_unlock(&soap_mutex));

		priv_soap_vcl = init_vcl();
		priv->priv = priv_soap_vcl;
		priv->free = clean_vcl;
		break;
	case VCL_EVENT_WARM:
		break;
	case VCL_EVENT_COLD:
		break;
	case VCL_EVENT_DISCARD:
		AZ(pthread_mutex_lock(&soap_mutex));
		if(0 == --refcount) {
			clean_xml();
			clean_apr();
		}
		AZ(pthread_mutex_unlock(&soap_mutex));
		break;
	default:
		return (0);
	}
	return (0);
}

sess_record* priv_soap_get(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
	struct priv_soap_task *priv_soap_task;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	if(priv->priv == NULL) {
		priv->priv = init_task(ctx);
		priv->free = clean_task;
	}
	CAST_OBJ_NOTNULL(priv_soap_task, priv->priv, PRIV_SOAP_TASK_MAGIC);
	return (priv_soap_task);
}

VCL_STRING __match_proto__(td_soap_action)
	vmod_action(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
	struct priv_soap_task *soap_task = priv_soap_get(ctx, priv);
	if(process_request(soap_task, HEADER) == 0) {
		return (soap_task->req_xml->action_name);
	}
	return ("TODO: ERROR");
}

VCL_STRING __match_proto__(td_soap_action_namespace)
	vmod_action_namespace(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
	struct priv_soap_task *soap_task = priv_soap_get(ctx, priv);
	if(process_request(soap_task, HEADER) == 0) {
		return (soap_task->req_xml->action_namespace);
	}
	return ("TODO: ERROR");
}

VCL_VOID __match_proto__(td_soap_add_namespace)
	vmod_add_namespace(VRT_CTX, struct vmod_priv *priv /* PRIV_VCL */, VCL_STRING prefix, VCL_STRING uri)
{
	struct priv_soap_vcl	    *priv_soap_vcl;
	struct soap_namespace	    *namespace;

	AN(priv);
	CAST_OBJ_NOTNULL(priv_soap_vcl, priv->priv, PRIV_SOAP_VCL_MAGIC);
	ALLOC_OBJ(namespace, PRIV_SOAP_NAMESPACE_MAGIC);
	AN(namespace);

	namespace->prefix = prefix;
	namespace->uri = uri;
	VSLIST_INSERT_HEAD(&priv_soap_vcl->namespaces, namespace, list);
}

VCL_STRING __match_proto__(td_soap_xpath_header)
	vmod_xpath_header(VRT_CTX, struct vmod_priv *priv_vcl /* PRIV_VCL */, struct vmod_priv *priv_task /* PRIV_TASK */, VCL_STRING xpath)
{
	struct priv_soap_vcl *soap_vcl;;
	struct priv_soap_task *soap_task;

	AN(priv_vcl);
	CAST_OBJ_NOTNULL(soap_vcl, priv_vcl->priv, PRIV_SOAP_VCL_MAGIC);

	AN(priv_task);
	soap_task = priv_soap_get(ctx, priv_task);

	if(!process_request(soap_task, HEADER)) {
		return ("TODO: make SOAP error?");
	}
	return (evaluate_xpath(soap_vcl, soap_task->req_xml->header, xpath));
}

VCL_STRING __match_proto__(td_soap_xpath_body)
	vmod_xpath_body(VRT_CTX, struct vmod_priv *priv_vcl /* PRIV_VCL */, struct vmod_priv *priv_task /* PRIV_TASK */, VCL_STRING xpath)
{
	return ("");
}

VCL_VOID __match_proto__(td_soap_synthetic)
	vmod_synthetic(VRT_CTX, struct vmod_priv *priv_task /* PRIV_TASK */, VCL_INT soap_code, VCL_STRING soap_message)
{
	struct priv_soap_task *priv_soap_task;

	AN(priv_task);
	priv_soap_task = priv_soap_get(ctx, priv_task);

	VRT_synth_page(ctx, "<soap>synth error</soap>");
}

VCL_VOID __match_proto__(td_soap_cleanup)
	vmod_cleanup(VRT_CTX, struct vmod_priv *priv_task /* PRIV_TASK */)
{
	struct priv_soap_task *priv_soap_task;

	AN(priv_task);
	priv_soap_task = priv_soap_get(ctx, priv_task);

	if(priv_soap_task->req_http) {
		clean_req_http(priv_soap_task->req_http);
		priv_soap_task->req_http = NULL;
	}
}
