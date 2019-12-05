/*
 * Copyright (c) 2019, Refinitiv
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
fill_pipeline(struct soap_req_http *req_http, struct http_conn *htc, body_part *pipeline, ssize_t bytes_read, ssize_t bytes_total)
{
	char *buf;
	ssize_t original_pipeline_len;
	ssize_t i = 0;
	ssize_t bytes_left;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	assert(bytes_total > bytes_read);
	bytes_left = bytes_total - bytes_read;
	original_pipeline_len = 0;
        VSLb(req_http->ctx->vsl, SLT_Debug, "fill_pipeline, bytes_read=%ld, total=%ld, pipeline_b=%p, pipeline_e=%p, pipeline_len=%ld, pipeline_content=%.*s", bytes_read, bytes_total, htc->pipeline_b, htc->pipeline_e, htc->pipeline_e - htc->pipeline_b, (int)(htc->pipeline_e - htc->pipeline_b), htc->pipeline_b ? htc->pipeline_b : "");
	if (htc->pipeline_b) {
		original_pipeline_len = htc->pipeline_e - htc->pipeline_b;
		assert(original_pipeline_len > 0);
		// If varnish already fill pipeline with all necessary data
		// fill pipeline variable with it, and skip call to read
		if (original_pipeline_len >= bytes_left + bytes_read) {
			pipeline->data = htc->pipeline_b + bytes_read;
			pipeline->length = bytes_left;
			return bytes_left;
		}
		buf = (char*)apr_palloc(req_http->pool, bytes_left + original_pipeline_len);
		XXXAN(buf);

		memcpy(buf, htc->pipeline_b, original_pipeline_len);
	}
	else {
		buf = (char*)apr_palloc(req_http->pool, bytes_left);
		XXXAN(buf);
	}
	htc->pipeline_b = buf;
	htc->pipeline_e = buf + original_pipeline_len;

	i = read(htc->fd, htc->pipeline_e, bytes_left);
	if (i <= 0) {
		if (htc->pipeline_b == htc->pipeline_e) {
			htc->pipeline_b = NULL;
			htc->pipeline_e = NULL;
		}
		// XXX: VTCP_Assert(i); // but also: EAGAIN
		VSLb(req_http->ctx->vsl, SLT_FetchError,
		    "%s", strerror(errno));
		req_http->ctx->req->req_body_status = REQ_BODY_FAIL;
		return (i);
	}
	pipeline->data = htc->pipeline_b + bytes_read;
	pipeline->length = original_pipeline_len - bytes_read + i;
	htc->pipeline_e = htc->pipeline_e + i;

	return (pipeline->length);
}

/* -------------------------------------------------------------------------------------/
   Read body part from varnish pipeline and uncompress the data if necessary
*/
int read_body_part(struct soap_req_http *req_http, ssize_t bytes_read, ssize_t bytes_total)
{
	body_part pipeline;
	int res;

	res = fill_pipeline(req_http, req_http->ctx->req->htc, &pipeline, bytes_read, bytes_total);
	if (res <= 0)
	{
		VSLb(req_http->ctx->vsl, SLT_Error, "v1_read error (%d bytes)", res);
		return res;
	}
	VSLb(req_http->ctx->vsl, SLT_Debug, "v1_read %d bytes", res);
	if (req_http->encoding == CE_NONE) {
		req_http->body = pipeline;
	}
	else if (uncompress_body_part(req_http, &pipeline, &req_http->body) != 0) {
		VSLb(req_http->ctx->vsl, SLT_Error, "Can't uncompress gzip body");
		return -1;
	}
	return res;
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
