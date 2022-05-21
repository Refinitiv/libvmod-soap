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

#include "config.h"

#include "cache/cache.h"
#include "vmod_soap.h"
#include "vcc_soap_if.h"

#include "vmod_soap_http.h"
#include "vmod_soap_gzip.h"

/* ------------------------------------------------------/
   Init HTTP context with encoding type
*/
void init_gzip(struct soap_req_http *req_http)
{
	VRT_CTX;
	int init_flags = MAX_WBITS;

	ctx = req_http->ctx;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	switch (req_http->encoding) {
	case CE_GZIP:
		init_flags += 16;
	case CE_DEFLATE:
		req_http->compression_stream =
		    (z_stream*)WS_Alloc(ctx->ws, sizeof(z_stream));
		XXXAN(req_http->compression_stream);
		XXXAZ(inflateInit2(req_http->compression_stream, init_flags));
		break;
	default:
		req_http->compression_stream = NULL;
		break;
	}
}

/* ----------------------------------------------------/
   HTTP context cleanup
*/
void clean_gzip(struct soap_req_http *req_http)
{
	AN(req_http);
	if (req_http->compression_stream) {
		inflateEnd(req_http->compression_stream);
		req_http->compression_stream = NULL;
	}
}

/* -------------------------------------------------------------------------------------/
   Decompress one part
*/
int uncompress_body_part(struct soap_req_http *req_http, body_part *compressed_body_part, body_part *uncompressed_body_part)
{
	VRT_CTX;
	z_stream	*stream;
	char		*buf;
	unsigned	len;
	int		sts = 0;

	if (compressed_body_part->length == 0)
		return (0);

	ctx = req_http->ctx;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	stream = req_http->compression_stream;
	AN(stream);

	len = WS_ReserveAll(ctx->ws);
	XXXAN(len);
	stream->avail_out = len;

	buf = WS_Reservation(ctx->ws);
	AN(buf);
	stream->next_out = (Bytef *)buf;

	stream->next_in = (Bytef *)compressed_body_part->data;
	stream->avail_in = compressed_body_part->length;

	AN(stream->avail_in);

	while(stream->avail_in > 0) {
		int err = inflate(stream, Z_SYNC_FLUSH);
		if ((err != Z_OK && err != Z_STREAM_END) ||
		    (err == Z_STREAM_END && stream->avail_in != 0))
		{
			sts = 1;
			break;
		}
	}
	assert(stream->avail_out <= len);
	len -= stream->avail_out;

	uncompressed_body_part->data = buf;
	uncompressed_body_part->length = len;

	WS_Release(ctx->ws, len);
	return sts;
}
