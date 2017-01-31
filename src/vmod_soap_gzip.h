#ifndef __VMOD_SOAP_GZIP_H__
#define __VMOD_SOAP_GZIP_H__

#include <lib/libvgz/vgz.h>

int init_http_context(http_context **ctx, enum ce_type compression ,apr_pool_t *pool);
int uninit_http_context(http_context **ctx);
int uncompress_body_part(z_stream *stream, body_part *compressed_body_part, body_part *uncompressed_body_part, apr_pool_t *pool);

#endif
