#include "vmod_soap.h"
#include "vmod_soap_xml.h"
#include "vmod_soap_gzip.h"

/* -------------------------------------------------------------------------------------/
    store SOAP error
*/
void add_soap_error(sess_record *r, int status, const char* fmt, ...)
{
    soap_error_info* sei = NULL;

    va_list args;
    va_start(args,fmt);

    if (!r->error_info) {
        r->error_info = (error_info*)apr_palloc(r->pool, sizeof(soap_error_info));
        memset(r->error_info, 0, sizeof(soap_error_info));
    }
    sei = (soap_error_info*)r->error_info;
    sei->ei.status = status;
    sei->ei.message = apr_pvsprintf(r->pool, fmt, args);
    sei->ei.synth_error = synth_soap_fault;
    if (!sei->soap_version) sei->soap_version = SOAP11;

    va_end(args);
}

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
int read_body_part(VRT_CTX, http_context* soap_ctx, int bytes_left, body_part *uncompressed_body_part)
{
    char buf[BUFFER_SIZE];
    body_part *read_part;
    if (soap_ctx->compression == CE_UNKNOWN)
    {
        return -1;
    }
    int bytes_to_read = bytes_left > BUFFER_SIZE ? BUFFER_SIZE : bytes_left;
    int bytes_read = v1f_read(soap_ctx->htc->vfc, soap_ctx->htc, buf, bytes_to_read);
    if (bytes_read <= 0)
    {
        return bytes_read;
    }
    read_part = (body_part*)apr_palloc(soap_ctx->pool, sizeof(body_part));
    if (read_part == 0)
    {
        return -1;
    }
    read_part->length = bytes_read;
    read_part->data = apr_pmemdup(soap_ctx->pool, buf, bytes_read);
    APR_ARRAY_PUSH(soap_ctx->body, body_part*) = read_part;
    if(uncompressed_body_part == NULL)
    {
         return bytes_read;
    }
    else if (soap_ctx->compression == CE_NONE)
    {
        memcpy(uncompressed_body_part, read_part, sizeof(body_part));
        return bytes_read;
    }
    else if (uncompress_body_part(soap_ctx->compression_stream, read_part, uncompressed_body_part, soap_ctx->pool) == 0)
    {
        return bytes_read;
    }
    return -1;
}

/* -------------------------------------------------------------------------------------/
    Convert set of body parts into one lineary arranged array
*/
int convert_parts(sess_record *s, apr_array_header_t *parts, char **buf)
{
    int i;
    int length = 0;
    for (i = 0; i < parts->nelts; i++)
    {
        length += APR_ARRAY_IDX(parts, i, body_part*)->length;
    }
    *buf = (char*)apr_palloc(s->pool, length);
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
void return_buffer(sess_record *s, struct http_conn* htc, char* base, char* end)
{
    apr_pool_t *pool = s->pool;
    int size = end - base;
    char *new_content;
    if (htc->pipeline_b != 0)
    {
        size += htc->pipeline_e - htc->pipeline_b;
        new_content = (char*)apr_palloc(pool, size);
        memcpy(new_content, base, end - base);
        memcpy(new_content + (end - base), htc->pipeline_b, htc->pipeline_e - htc->pipeline_b);
    }
    else
    {
        new_content = base;
    }
    htc->pipeline_b = new_content;
    htc->pipeline_e = new_content + size;
}

/* -------------------------------------------------------------------------------------/
    Return set of body parts back to varnish internal pipeline
*/
void return_parts(sess_record *s, struct http_conn* htc, apr_array_header_t *body_parts)
{
    char *buf;
    int offset = convert_parts(s, body_parts, &buf);
    return_buffer(s, htc, buf, buf + offset);
}

int process_soap_request(sess_record* r)
{
    soapparse_cb cb;
    unsigned long cl;

    const char* content_length;

    if (!http_GetHdr(r->ctx->http_req, H_Content_Length, &content_length)) {
        add_soap_error(r, 400, "Request without Content-Length header is not supported.");
        return 1;
    }
    cl = strtoul(content_length, NULL, 10);
    if (0 >= cl) {
        add_soap_error(r, 400, "Request with invalid Content-Length header value");
        return 1;
    }

    int rc = parse_soap_envelope(r, 1, cl, &cb);

    // check if soap version available
    if (cb.soap_version)
    {
        soap_error_info *info = (soap_error_info*)r->error_info;
        if (!info) {
            info = (soap_error_info*)apr_palloc(r->pool, sizeof(soap_error_info));
            memset(info, 0, sizeof(soap_error_info));
            r->error_info = (error_info*)info;
        }
        info->soap_version = cb.soap_version;
    }

    if (rc)
        return 1;

    return 0;
}
