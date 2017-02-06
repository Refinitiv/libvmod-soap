#ifndef __VMOD_SOAP_XML_H__
#define __VMOD_SOAP_XML_H__

#include <libxml/tree.h>
#include <libxml/parser.h>

xmlSAXHandler default_sax_handler;
xmlSAXHandler soap_sax_handler;

typedef struct _elem_info
{
    const xmlChar *localname;
    const xmlChar *ns;
} elem_info;

typedef struct _envelope_info
{
    int soap_header_begin_offset;
    int soap_header_end_offset;
} envelope_info;

typedef struct _sax_parser_ctx
{
    int stop;
    const char* error;
    int soap_version;
    int level;

    apr_array_header_t *parent_stack;
    apr_pool_t *pool;
    struct ws *ws;

    const char* request_namespace;
    const char* request_name;

    xmlNodePtr    header;
    xmlNodePtr    body;
    envelope_info buffer_info;
} sax_parser_ctx;

int synth_soap_fault(sess_record *r);
int parse_soap_envelope(sess_record* r, int request, unsigned long content_length, soapparse_cb* cb);
void init_xml();
void clean_xml();

#endif
