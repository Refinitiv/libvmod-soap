#ifndef __VMOD_SOAP_GZIP_H__
#define __VMOD_SOAP_GZIP_H__

#include <lib/libvgz/vgz.h>

void init_gzip(struct soap_req_http *req_http);
void clean_gzip(struct soap_req_http *req_http);
int uncompress_body_part(z_stream *stream, body_part *compressed_body_part, body_part *uncompressed_body_part, apr_pool_t *pool);

#endif
