#ifndef __VMOD_SOAP_HTTP_H__
#define __VMOD_SOAP_HTTP_H__

const char* http_status2str(const int status);
int http_content_encoding(struct http *http);
unsigned long http_content_length(struct http *http);

// Content-Encoding types
enum ce_type {
	CE_UNKNOWN = -1,
	CE_NONE = 0,
	CE_GZIP = 1,
	CE_DEFLATE =  2
};

#endif
