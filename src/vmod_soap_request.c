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
#include "vmod_soap_xml.h"
#include "vmod_soap_gzip.h"

static ssize_t
fill_pipeline(struct soap_req_http *req_http, const struct vfp_ctx *vc, struct http_conn *htc, ssize_t len)
{
	char *buf;
	ssize_t l;
	ssize_t i = 0;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	assert(len > 0);
	l = 0;
	if (htc->pipeline_b) {
		l = htc->pipeline_e - htc->pipeline_b;
		assert(l > 0);
		if (l > len) {
			return (l);
		}
		buf = (char*)apr_palloc(req_http->pool, len);
		XXXAN(buf);

		memcpy(buf, htc->pipeline_b, l);
		len -= l;
	}
	else {
		buf = (char*)apr_palloc(req_http->pool, len);
		XXXAN(buf);
	}
	htc->pipeline_b = buf;
	htc->pipeline_e = buf + l;
	if (len > 0) {
		i = read(htc->fd, htc->pipeline_e, len);
		if (i < 0) {
			if (htc->pipeline_b == htc->pipeline_e) {
				htc->pipeline_b = NULL;
				htc->pipeline_e = NULL;
			}
			// XXX: VTCP_Assert(i); // but also: EAGAIN
			VSLb(vc->wrk->vsl, SLT_FetchError,
			    "%s", strerror(errno));
			req_http->ctx->req->req_body_status = REQ_BODY_FAIL;
			return (i);
		}
		htc->pipeline_e = htc->pipeline_e + i;
	}
	return (i + l);
}

/* -------------------------------------------------------------------------------------/
   Read body part from varnish pipeline and uncompress the data if necessary
*/
int read_body_part(struct soap_req_http *req_http, int bytes_left)
{
	body_part pipeline;
	int bytes_to_read;
	int bytes_read;

	bytes_to_read = bytes_left > BUFFER_SIZE ? BUFFER_SIZE : bytes_left;
	bytes_read = fill_pipeline(req_http, req_http->ctx->req->htc->vfc, req_http->ctx->req->htc, bytes_to_read);
	if (bytes_read <= 0)
	{
		VSLb(req_http->ctx->vsl, SLT_Error, "v1_read error (%d bytes)", bytes_read);
		return bytes_read;
	}
	VSLb(req_http->ctx->vsl, SLT_Debug, "v1_read %d bytes", bytes_read);
	pipeline.data = req_http->ctx->req->htc->pipeline_b;
	pipeline.length = req_http->ctx->req->htc->pipeline_e - req_http->ctx->req->htc->pipeline_b;
	if (req_http->encoding == CE_NONE) {
		req_http->body = pipeline;
	}
	else if (uncompress_body_part(req_http, &pipeline, &req_http->body) != 0) {
		VSLb(req_http->ctx->vsl, SLT_Error, "Can't uncompress gzip body");
		return -1;
	}
	return bytes_read;
}

void init_req_http(struct soap_req_http *req_http)
{
	AN(req_http);
	req_http->encoding = http_content_encoding(req_http->ctx->http_req);
	if (req_http->encoding == CE_GZIP) {
		init_gzip(req_http);
	}
}

/* -------------------------------------------------------------------------------------/
   Return set of body parts back to varnish internal pipeline
*/
void clean_req_http(struct soap_req_http *req_http)
{
	AN(req_http);
	if (req_http->encoding == CE_GZIP) {
		if (req_http->body.data) {
			req_http->body.data = NULL;
			req_http->body.length = 0;
		}
		clean_gzip(req_http);
	}
}
