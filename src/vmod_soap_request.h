#ifndef __VMOD_SOAP_REQUEST_H__
#define __VMOD_SOAP_REQUEST_H__

void add_soap_error(sess_record *r, int status, const char* fmt, ...);
int read_body_part(VRT_CTX, http_context* soap_ctx, int bytes_left, body_part *uncompressed_body_part);
int convert_parts(sess_record *s, apr_array_header_t *parts, char **buf);
void return_parts(sess_record *s, struct http_conn* htc, apr_array_header_t *body_parts);
int process_soap_request(sess_record* r);


#endif
