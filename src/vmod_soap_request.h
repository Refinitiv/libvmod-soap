#ifndef __VMOD_SOAP_REQUEST_H__
#define __VMOD_SOAP_REQUEST_H__

typedef struct body_part {
	char *data;
	int length;
} body_part;

struct soap_req_http {
	unsigned		magic;
#define SOAP_REQ_HTTP_MAGIC 0x5B13F1F0
	VRT_CTX;
	apr_pool_t		*pool;

	unsigned int		cl;
	enum ce_type		encoding;

	apr_array_header_t	*bodyparts;   // HTTP payload stored as is
	z_stream		*compression_stream;
};

int read_body_part(struct soap_req_http *req_http, int bytes_left, body_part *uncompressed_body_part);
int convert_parts(struct soap_req_http *req_http, apr_array_header_t *parts, char **buf);

void init_req_http(struct soap_req_http *req_http);
void clean_req_http(struct soap_req_http *req_http);


#endif
