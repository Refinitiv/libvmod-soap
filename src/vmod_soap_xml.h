#ifndef __VMOD_SOAP_XML_H__
#define __VMOD_SOAP_XML_H__

#include <libxml/tree.h>
#include <libxml/parser.h>

struct sess_record;
struct error_info {
    int status;
    const char *message;
    int (*synth_error)(struct sess_record *r);
};

struct soap_error_info
{
    struct error_info ei;
    int soap_version;
};

typedef struct soap_req_xml {
	unsigned		magic;
#define SOAP_REQ_XML_MAGIC 0x5ABC310F
	VRT_CTX;
	apr_pool_t		*pool;

	int			stop;
	const char		*error;
	struct soap_error_info	*error_info;
	int			soap_version;
	int			level;

	apr_array_header_t	*parent_stack;
	const char		*action_namespace;
	const char		*action_name;
	xmlParserCtxtPtr	parser;
	xmlNodePtr		header;
	xmlNodePtr		body;
} sax_parser_ctx;

int synth_soap_fault(struct soap_req_xml *req_xml);
void init_xml();
void clean_xml();
void init_req_xml(struct soap_req_xml *req_xml);
void clean_req_xml(struct soap_req_xml *req_xml);

void parse_soap_chunk(struct soap_req_xml *soap_req_xml, const char *data, int length);
const char* evaluate_xpath(struct priv_soap_vcl *priv_soap_vcl, xmlNodePtr node, const char* xpath);

#endif
