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
	struct priv_soap_task *soap_task;

	ALLOC_OBJ(soap_task, PRIV_SOAP_TASK_MAGIC);
	AN(soap_task);

	soap_task->ctx = ctx;

	XXXAZ(apr_pool_create(&soap_task->pool, apr_pool));

	ALLOC_OBJ(soap_task->req_http, SOAP_REQ_HTTP_MAGIC);
	XXXAN(soap_task->req_http);

	ALLOC_OBJ(soap_task->req_xml, SOAP_REQ_XML_MAGIC);
	XXXAN(soap_task->req_xml);

	VSLb(soap_task->ctx->vsl, SLT_Debug, "init_task");
	return soap_task;
}

/* -----------------------------------------------------------------/
   destroy session
*/
static void clean_task(void *priv)
{
	struct priv_soap_task *soap_task;

	AN(priv);
	CAST_OBJ_NOTNULL(soap_task, priv, PRIV_SOAP_TASK_MAGIC);

	clean_req_xml(soap_task->req_xml);
	INIT_OBJ(soap_task->req_xml, SOAP_REQ_XML_MAGIC);

	clean_req_http(soap_task->req_http);
	INIT_OBJ(soap_task->req_http, SOAP_REQ_HTTP_MAGIC);

	INIT_OBJ(soap_task->req_xml, SOAP_REQ_XML_MAGIC);
	FREE_OBJ(soap_task->req_xml);

	INIT_OBJ(soap_task->req_http, SOAP_REQ_HTTP_MAGIC);
	FREE_OBJ(soap_task->req_http);

	AN(soap_task->pool);
	apr_pool_destroy(soap_task->pool);

	INIT_OBJ(soap_task, PRIV_SOAP_TASK_MAGIC);
	FREE_OBJ(soap_task);
}

int process_request(struct priv_soap_task *task, enum soap_state state)
{
	VSLb(task->ctx->vsl, SLT_Debug, "process_request 0: %d/%d", task->state, state);
	while (task->state < state) {
		switch (task->state) {
		case NONE:  // init
			VSLb(task->ctx->vsl, SLT_Debug, "process_request 1: %d/%d", task->state, state);
			task->req_http->pool = task->pool;
			task->req_http->ctx = task->ctx;
			init_req_http(task->req_http);
			if (task->req_http->encoding != CE_GZIP && task->req_http->encoding != CE_NONE) {
				VSLb(task->ctx->vsl, SLT_Error, "Unsupported Content-Encoding");
				return (-1);
			}

			task->req_xml->pool = task->pool;
			task->req_xml->ctx = task->ctx;
			init_req_xml(task->req_xml);

			task->bytes_total = http_GetContentLength(task->ctx->http_req);
			if(task->bytes_total <= 0) {
				VSLb(task->ctx->vsl, SLT_Error, "Invalid content-length %ld", task->bytes_total);
				return (-1);
			}
			task->state = INIT;
			break;
		case INIT:  // want header
		case HEADER:  // want body
			VSLb(task->ctx->vsl, SLT_Debug, "process_request 5: %d/%d (%ld bytes)", task->state, state, task->bytes_total);
			if (task->bytes_total <= 0) {
				VSLb(task->ctx->vsl, SLT_Error, "Not enough data");
				return (-1);
			}
			ssize_t bytes_read = 0;
			while (bytes_read < task->bytes_total) {
				int just_read = read_body_part(task->req_http, bytes_read, task->bytes_total);
				if (just_read <= 0) {
					VSLb(task->ctx->vsl, SLT_Error, "SOAP: http read failed (%d, errno: %d)", just_read, errno);
					return (-1);
				}
				bytes_read += just_read;
				VSLb(task->ctx->vsl, SLT_Debug, "process_request 6: read %d bytes", just_read);

				// parse chunk
				VSLb(task->ctx->vsl, SLT_Debug, "process_request 7: tota %d bytes", task->req_http->body.length);
				if (parse_soap_chunk(task->req_xml, task->req_http->body.data, task->req_http->body.length)) {
					VSLb(task->ctx->vsl, SLT_Error, "SOAP: soap read failed %d", errno);
					return (-1);
				}

				if (task->req_xml->body) {
					task->state = BODY;
					break;
				}
				else if (task->req_xml->header && task->req_xml->action_namespace && task->req_xml->action_name) {
					task->state = HEADER;
					break;
				}
			}
			break;
		case BODY:  // read from memory
		case DONE:
			VSLb(task->ctx->vsl, SLT_Debug, "process_request 8: %d/%d", task->state, state);
			break;
		default:
			VSLb(task->ctx->vsl, SLT_Debug, "process_request 9: %d/%d", task->state, state);
			break;
		}
	}
	VSLb(task->ctx->vsl, SLT_Debug, "process_request .: %d/%d", task->state, state);
	return (0);
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
	struct priv_soap_task *soap_task;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	if(priv->priv == NULL) {
		priv->priv = init_task(ctx);
		priv->free = clean_task;
	}
	CAST_OBJ_NOTNULL(soap_task, priv->priv, PRIV_SOAP_TASK_MAGIC);
	if(soap_task->ctx != ctx) {
		soap_task->ctx = ctx;
		if(soap_task->req_http) {
			soap_task->req_http->ctx = ctx;
		}
		if(soap_task->req_xml) {
			soap_task->req_xml->ctx = ctx;
		}
	}
	return (soap_task);
}

VCL_BOOL __match_proto__(td_soap_is_valid)
	vmod_is_valid(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
	struct priv_soap_task *soap_task = priv_soap_get(ctx, priv);

	return (process_request(soap_task, HEADER) == 0);
}

VCL_STRING __match_proto__(td_soap_action)
	vmod_action(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
	struct priv_soap_task *soap_task = priv_soap_get(ctx, priv);
	if(process_request(soap_task, HEADER) == 0) {
		return (soap_task->req_xml->action_name);
	}
	return ("");
}

VCL_STRING __match_proto__(td_soap_action_namespace)
	vmod_action_namespace(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
	struct priv_soap_task *soap_task = priv_soap_get(ctx, priv);
	if(process_request(soap_task, HEADER) == 0) {
		return (soap_task->req_xml->action_namespace);
	}
	return ("");
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

	if(process_request(soap_task, HEADER) == 0) {
		return (evaluate_xpath(soap_vcl, soap_task, soap_task->req_xml->header, xpath));
	}
	return ("");
}

VCL_STRING __match_proto__(td_soap_xpath_body)
	vmod_xpath_body(VRT_CTX, struct vmod_priv *priv_vcl /* PRIV_VCL */, struct vmod_priv *priv_task /* PRIV_TASK */, VCL_STRING xpath)
{
	struct priv_soap_vcl *soap_vcl;;
	struct priv_soap_task *soap_task;

	AN(priv_vcl);
	CAST_OBJ_NOTNULL(soap_vcl, priv_vcl->priv, PRIV_SOAP_VCL_MAGIC);

	AN(priv_task);
	soap_task = priv_soap_get(ctx, priv_task);

	if(process_request(soap_task, BODY) == 0) {
		return (evaluate_xpath(soap_vcl, soap_task, soap_task->req_xml->body, xpath));
	}
	return ("");
}

VCL_VOID __match_proto__(td_soap_synthetic)
	vmod_synthetic(VRT_CTX, struct vmod_priv *priv_task /* PRIV_TASK */, VCL_INT soap_code, VCL_STRING soap_message)
{
	struct priv_soap_task *soap_task;

	AN(priv_task);
	soap_task = priv_soap_get(ctx, priv_task);

	synth_soap_fault(soap_task->req_xml, soap_code, soap_message);
}
