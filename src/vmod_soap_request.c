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
v1f_read(const struct vfp_ctx *vc, struct http_conn *htc, void *d, ssize_t len)
{
	ssize_t l;
	unsigned char *p;
	ssize_t i = 0;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	assert(len > 0);
	l = 0;
	p = d;
	if (htc->pipeline_b) {
		l = htc->pipeline_e - htc->pipeline_b;
		assert(l > 0);
		if (l > len)
			l = len;
		memcpy(p, htc->pipeline_b, l);
		p += l;
		len -= l;
		htc->pipeline_b += l;
		if (htc->pipeline_b == htc->pipeline_e)
			htc->pipeline_b = htc->pipeline_e = NULL;
	}
	if (len > 0) {
		i = read(htc->fd, p, len);
		if (i < 0) {
			// XXX: VTCP_Assert(i); // but also: EAGAIN
			VSLb(vc->wrk->vsl, SLT_FetchError,
			    "%s", strerror(errno));
			return (i);
		}
	}
	return (i + l);
}


/* -------------------------------------------------------------------------------------/
   Read body part from varnish pipeline and uncompress the data if necessary
*/
int read_body_part(struct soap_req_http *req_http, int bytes_left, body_part *uncompressed_body_part)
{
	char buf[BUFFER_SIZE]; // TODO: read varnish gzip buffer size?
	body_part *read_part;
	if (req_http->encoding == CE_UNKNOWN) {
		VSLb(req_http->ctx->vsl, SLT_Error, "Unknown Content-Encoding");
		return -1;
	}
	int bytes_to_read = bytes_left > BUFFER_SIZE ? BUFFER_SIZE : bytes_left;
	int bytes_read = v1f_read(req_http->ctx->req->htc->vfc, req_http->ctx->req->htc, buf, bytes_to_read);
	if (bytes_read <= 0)
	{
		return bytes_read;
	}
	read_part = (body_part*)apr_palloc(req_http->pool, sizeof(body_part));
	if (read_part == 0)
	{
		VSLb(req_http->ctx->vsl, SLT_Error, "Can't alloc memory (%ld bytes)", sizeof(body_part));
		return -1;
	}
	read_part->length = bytes_read;
	read_part->data = apr_pmemdup(req_http->pool, buf, bytes_read);
	APR_ARRAY_PUSH(req_http->bodyparts, body_part*) = read_part;
	if(uncompressed_body_part == NULL)
	{
		return bytes_read;
	}
	else if (req_http->encoding == CE_NONE)
	{
		memcpy(uncompressed_body_part, read_part, sizeof(body_part));
		return bytes_read;
	}
	else if (uncompress_body_part(req_http->compression_stream, read_part, uncompressed_body_part, req_http->pool) == 0)
	{
		return bytes_read;
	}
	VSLb(req_http->ctx->vsl, SLT_Error, "Can't uncompress gzip body");
	return -1;
}

/* -------------------------------------------------------------------------------------/
   Convert set of body parts into one lineary arranged array
*/
int convert_parts(struct soap_req_http *req_http, apr_array_header_t *parts, char **buf)
{
	int i;
	int length = 0;
	for (i = 0; i < parts->nelts; i++)
	{
		length += APR_ARRAY_IDX(parts, i, body_part*)->length;
	}
	*buf = (char*)apr_palloc(req_http->pool, length);
	int offset = 0;
	for (i = 0; i < parts->nelts; i++)
	{
		memcpy(*buf + offset, APR_ARRAY_IDX(parts, i, body_part*)->data, APR_ARRAY_IDX(parts, i, body_part*)->length);
		offset += APR_ARRAY_IDX(parts, i, body_part*)->length;
	}
	return length;
}

/* -----------------------------------------------------------------
   --------------------/
   Return data array back to varnish internal pipeline
*/
void return_buffer(struct soap_req_http *req_http, struct http_conn* htc, char* base, char* end)
{
	int size = end - base;
	char *new_content;

	VSLb(req_http->ctx->vsl, SLT_Debug, "return_buffer 0: %ld", htc->pipeline_e - htc->pipeline_b);
	if (htc->pipeline_b != 0)
	{
		size += htc->pipeline_e - htc->pipeline_b;
		new_content = (char*)apr_palloc(req_http->pool, size);
		memcpy(new_content, base, end - base);
		memcpy(new_content + (end - base), htc->pipeline_b, htc->pipeline_e - htc->pipeline_b);
	}
	else
	{
		new_content = base;
	}
	htc->pipeline_b = new_content;
	htc->pipeline_e = new_content + size;
	VSLb(req_http->ctx->vsl, SLT_Debug, "return_buffer 0: %ld", htc->pipeline_e - htc->pipeline_b);
}

void init_req_http(struct soap_req_http *req_http)
{
	req_http->cl = http_content_length(req_http->ctx->http_req);
	req_http->encoding = http_content_encoding(req_http->ctx->http_req);
	req_http->bodyparts = apr_array_make(req_http->pool, 16, sizeof(body_part*));
	XXXAN(req_http->bodyparts);

	init_gzip(req_http);
}

/* -------------------------------------------------------------------------------------/
   Return set of body parts back to varnish internal pipeline
*/
void clean_req_http(struct soap_req_http *req_http)
{
	char *buf;
	int offset;

	AN(req_http);
	AN(req_http->bodyparts);
	offset = convert_parts(req_http, req_http->bodyparts, &buf);
	return_buffer(req_http, req_http->ctx->req->htc, buf, buf + offset);
	req_http->bodyparts = NULL;

	clean_gzip(req_http);
}
