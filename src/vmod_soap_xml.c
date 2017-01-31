#include "vmod_soap.h"
#include "vmod_soap_xml.h"
#include "vmod_soap_http.h"
#include "vmod_soap_request.h"


const char* soap_versions[] = {0, "http://schemas.xmlsoap.org/soap/envelope/", "http://www.w3.org/2003/05/soap-envelope"};

/* -------------------------------------------------------------------------------------/
    Determines closing tag size in current XML encoding
*/
static int get_closing_tag_size(xmlParserCtxtPtr xml_ctx)
{
    if (!xml_ctx || !xml_ctx->input || !xml_ctx->input->buf || 
        !xml_ctx->input->buf->encoder || !xml_ctx->input->buf->encoder->output) 
        return 1;
    
    unsigned char out[4];
    unsigned char *in = ">";
    int in_size = 1;
    int out_size = 4;

    int res = xml_ctx->input->buf->encoder->output(out, &out_size, in, &in_size);
    return out_size;
}

/* -------------------------------------------------------------------------------------/
    Push one element
*/
static void push_element(sax_parser_ctx* ctx, const xmlChar* localname, const xmlChar* ns)
{
    elem_info* info = (elem_info*)apr_palloc(ctx->pool, sizeof(elem_info));
    info->localname = localname;
    info->ns = ns;
    APR_ARRAY_PUSH(ctx->parent_stack, elem_info*) = info;
}

/* -------------------------------------------------------------------------------------/
    Start element SAX handler
*/
static void start_element_ns(void *ptr,
                     const xmlChar *localname,
                     const xmlChar *prefix,
                     const xmlChar *URI,
                     int nb_namespaces,
                     const xmlChar **namespaces,
                     int nb_attributes,
                     int nb_defaulted,
                     const xmlChar **attributes)
{
    xmlParserCtxtPtr xml_parser = (xmlParserCtxtPtr)ptr;
    sax_parser_ctx* sax_ctx = (sax_parser_ctx*)xml_parser->_private;
    int level = sax_ctx->parent_stack->nelts;
    elem_info *parent = 0;
    
    switch (level) //level means element depth
    {
        case 0:  //null depth only envelope is acceptable
            if (xmlStrEqual(localname, "Envelope"))
            {
                if (xmlStrstr(URI, soap_versions[SOAP11]) != 0)
                    sax_ctx->soap_version = SOAP11;
                else if (xmlStrstr(URI, soap_versions[SOAP12]) != 0)
                    sax_ctx->soap_version = SOAP12;
                else
                {
                    sax_ctx->error = "Invalid SOAP envelope namespace found";
                    xmlStopParser(xml_parser);
                    return;
                }
                sax_ctx->buffer_info.soap_header_begin_offset = xmlByteConsumed(xml_parser) + get_closing_tag_size(xml_parser);
            }
            else
            {
                sax_ctx->error = "First element must be SOAP Envelope";
                xmlStopParser(xml_parser);
                return;
            }
            break;
        case 1:
            if (xmlStrEqual(localname, "Header") && xmlStrEqual(URI, soap_versions[sax_ctx->soap_version]))
                sax_ctx->header = xml_parser->node;
            else if (xmlStrEqual(localname, "Body") && xmlStrEqual(URI, soap_versions[sax_ctx->soap_version]))
                sax_ctx->body = xml_parser->node;
            else {
                sax_ctx->error = "Invalid SOAP element found";
                xmlStopParser(xml_parser);
                return;
            }
            break;
        case 2:
            parent = APR_ARRAY_IDX(sax_ctx->parent_stack, level - 1, elem_info*);
            //looking for routing element - first child of soap:Body
            if (xmlStrEqual(parent->localname, "Body") && !sax_ctx->request_name ) 
            {
                sax_ctx->request_namespace = (xmlChar*)apr_pstrdup(sax_ctx->pool,(char*)URI);
                sax_ctx->request_name = (xmlChar*)apr_pstrdup(sax_ctx->pool,(char*)localname);
                int len = xmlStrlen(URI) - 1;
                if ( sax_ctx->request_namespace && sax_ctx->request_namespace[len] == '/') ((char*)sax_ctx->request_namespace)[len] = '\0';
            }
            break;
    }
    push_element(sax_ctx, localname, URI);

    default_sax_handler.startElementNs(ptr,localname, prefix, URI, nb_namespaces, namespaces, nb_attributes, nb_defaulted, attributes);
}

/* -------------------------------------------------------------------------------------/
    End element SAX handler
*/
static void end_element_ns (void *ptr,
                     const xmlChar *localname,
                     const xmlChar *prefix,
                     const xmlChar *URI)
{
    xmlParserCtxtPtr xml_parser = (xmlParserCtxtPtr)ptr;
    sax_parser_ctx* sax_ctx = (sax_parser_ctx*)xml_parser->_private;
    
    apr_array_pop(sax_ctx->parent_stack);   
    if (xmlStrEqual(localname, "Header") && xmlStrstr(URI, soap_versions[sax_ctx->soap_version]))
    {
        sax_ctx->header = xml_parser->node;
        sax_ctx->buffer_info.soap_header_end_offset = xmlByteConsumed(xml_parser);
    }
    else if (xmlStrEqual(localname, "Body") && xmlStrstr(URI, soap_versions[sax_ctx->soap_version]))
    {
        sax_ctx->body = xml_parser->node;
        sax_ctx->stop = 1;
        xmlStopParser(xml_parser);
    }    
    
    default_sax_handler.endElementNs(ptr, localname, prefix, URI);
}

/* -------------------------------------------------------------------------------------/
    Init default and custom SAX handlers
*/
int init_soap_sax_handler()
{
    memset(&default_sax_handler, 0, sizeof(default_sax_handler));
    memset(&soap_sax_handler, 0, sizeof(soap_sax_handler));
    xmlSAX2InitDefaultSAXHandler(&default_sax_handler, 0);
    xmlSAX2InitDefaultSAXHandler(&soap_sax_handler, 0);
    soap_sax_handler.startElementNs = &start_element_ns;
    soap_sax_handler.endElementNs = &end_element_ns;
    soap_sax_handler.error = NULL;

    return 0;
}

/* -------------------------------------------------------------------------------------/
    create v11 or v12 soap fault
*/
static xmlNodePtr create_soap_fault(xmlDocPtr doc, soap_error_info *info)
{
    xmlChar *soap_namespace;
    if (info->soap_version == SOAP11)
    {
        soap_namespace = soap_versions[SOAP11];
    }
    else
    {
        soap_namespace = soap_versions[SOAP12];
    }

    if (!info->ei.message)
        info->ei.message = "Internal Server Error";

    xmlNsPtr soap_ns = xmlNewNs(0, soap_namespace, "soap");
    xmlNodePtr soap_envelope = xmlNewNode(soap_ns, "Envelope");
    soap_envelope->nsDef = soap_ns;
    xmlDocSetRootElement(doc, soap_envelope);

    xmlNodePtr soap_body = xmlNewNode(soap_ns, "Body");
    xmlAddChild(soap_envelope, soap_body);

    xmlNodePtr soap_fault = xmlNewNode(soap_ns, "Fault");
    xmlAddChild(soap_body, soap_fault);

    if (info->soap_version == SOAP12)
    {
        xmlNodePtr fault_code = xmlNewNode(soap_ns, "Code");
        xmlAddChild(soap_fault, fault_code);
        xmlNewTextChild(fault_code, soap_ns, "Value", "soap:Receiver");

        xmlNodePtr fault_reason = xmlNewNode(soap_ns, "Reason");
        xmlAddChild(soap_fault, fault_reason);
        xmlNodePtr text = xmlNewTextChild(fault_reason, soap_ns, "Text", info->ei.message);
        xmlNewProp(text, "xml:lang", "en-US");
    }
    else
    {
        xmlNewTextChild(soap_fault, soap_ns, "faultcode", "soap:Receiver");
        xmlNewTextChild(soap_fault, soap_ns, "faultstring", info->ei.message);
    }
    return soap_fault;
}

/* -------------------------------------------------------------------------------------/
    synth SOAP fault
*/
int synth_soap_fault(sess_record *r)
{
    soap_error_info *info = (soap_error_info*)r->error_info;
    if (info != 0)
    {
        if (0 == info->ei.status)
            info->ei.status = 500;

        VRT_l_resp_status(r->ctx, info->ei.status);
        VRT_l_resp_reason(r->ctx, http_status2str(info->ei.status), vrt_magic_string_end );
        xmlDocPtr doc = xmlNewDoc("1.0");
        xmlNodePtr soap_fault = create_soap_fault(doc, info);
        xmlChar *content;
        int length;
        xmlDocDumpMemory(doc, &content, &length);
        VRT_synth_page(r->ctx, content, vrt_magic_string_end);
        xmlFree(content);
        xmlFreeDoc(doc);
    }
    else
    {
        VRT_l_resp_status(r->ctx, 500);
        VRT_l_resp_reason(r->ctx, http_status2str(500), vrt_magic_string_end );
    }
    return 0;
}

/* -------------------------------------------------------------------------------------/
    Main parser routine
    Parses enevelope until the first child of soap:Body
    When element is found calls on_header handler, if body is required by some of the headers
    then parses intil the end of soap:Body and calls on_body handler
*/
int parse_soap_envelope(sess_record* r, int request, unsigned long content_length, soapparse_cb* cb)
{
    int status = HANDLER_SUCCESS_NOT_MODIFIED;
    int rc = 1;

    http_context *http_ctx = NULL;

    sax_parser_ctx sax_ctx;
    memset(&sax_ctx, 0, sizeof(sax_ctx));
    xmlParserCtxtPtr xml_parser = xmlCreatePushParserCtxt(&soap_sax_handler, 0, 0, 0, 0);
    xml_parser->_private = &sax_ctx;
    sax_ctx.pool = r->pool;
    sax_ctx.parent_stack = apr_array_make(r->pool, 16, sizeof(elem_info*));

    cb->soap_version = 0;

    int compression = http_content_encoding(r->ctx->http_req);

    if (compression == CE_UNKNOWN) {
        add_soap_error(r, 500, "Unknown content-encoding value");
        goto E_x_i_t;
    }
    if ( init_http_context(&http_ctx, compression, r->pool) )
    {
        add_soap_error(r, 500, "Failed to initialize HTTP context");
        goto E_x_i_t;
    }
    http_ctx->htc = r->ctx->req->htc;
    while(content_length > 0)
    {
        body_part uncompressed_body_part;        
        int bytes_read = read_body_part(r->ctx, http_ctx, content_length, &uncompressed_body_part);
        if (bytes_read <= 0)
        {
            add_soap_error(r, 500, "Error reading soap, err: %d", errno );
            goto E_x_i_t;
        }
        content_length -= bytes_read;
        
        // parse chunk
        int err = xmlParseChunk(xml_parser, uncompressed_body_part.data, uncompressed_body_part.length, 0);
        
        // real error occured
        if (sax_ctx.error || (err && !sax_ctx.stop))
        {
            const char* message = "Unknown Error";
            if (sax_ctx.error) {
                message = sax_ctx.error;
            }
            else {
                xmlErrorPtr xml_error = xmlCtxtGetLastError(xml_parser);
                if (xml_error)
                    message = xml_error->message;
            }
            
            add_soap_error(r, 500, "SOAP parsing failed: %s", message);
            
            goto E_x_i_t;
        }

        // headers and request info available
        if(sax_ctx.request_name)
        {/*
            if (cb->hp) {
                status = cb->hp(sax_ctx.request_name, sax_ctx.request_namespace, sax_ctx.header, r);
                cb->hp = NULL;
                if (HANDLER_ERROR == status) 
                    goto E_x_i_t;
                
                if (0 == (status & HANDLER_SUCCESS_NEEDBODY) || !cb->ba){
                    break;
                }
            }

            // body available
            if (sax_ctx.body && sax_ctx.stop)
            {
                if (0 != (status & HANDLER_SUCCESS_NEEDBODY)){
                        if (cb->ba) {
                        status |= cb->ba(sax_ctx.request_name, sax_ctx.request_namespace, sax_ctx.header, sax_ctx.body, r);
                        if (HANDLER_ERROR == status) 
                            goto E_x_i_t;
                            }
                    break;
                }
            }
         */}
    }
    
    return_parts(r, http_ctx->htc, http_ctx->body);
    rc = 0;

E_x_i_t:

    cb->soap_version = sax_ctx.soap_version;
    uninit_http_context(&http_ctx);
    if (xml_parser->myDoc) xmlFreeDoc(xml_parser->myDoc);
    xmlFreeParserCtxt(xml_parser);
    return rc;
}
