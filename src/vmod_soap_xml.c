/*
 * Copyright 2017 Thomson Reuters
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

#include "vmod_soap.h"
#include "vmod_soap_xml.h"
#include "vmod_soap_http.h"
#include "vmod_soap_request.h"
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <syslog.h>


const char* soap_versions[] = {0, "http://schemas.xmlsoap.org/soap/envelope/", "http://www.w3.org/2003/05/soap-envelope"};

static xmlSAXHandler default_sax_handler;
static xmlSAXHandler soap_sax_handler;

typedef struct _elem_info
{
	const xmlChar *localname;
	const xmlChar *ns;
} elem_info;


/* -------------------------------------------------------------------------------------/
   store SOAP error
*/
void add_soap_error(struct soap_req_xml *req_xml, int status, const char* fmt, ...)
{
	struct soap_error_info* sei = NULL;

	va_list args;
	va_start(args,fmt);

	if (!req_xml->error_info) {
		req_xml->error_info = (struct soap_error_info*)apr_palloc(req_xml->pool, sizeof(struct soap_error_info));
		memset(req_xml->error_info, 0, sizeof(struct soap_error_info));
	}
	sei = (struct soap_error_info*)req_xml->error_info;
	sei->ei.status = status;
	sei->ei.message = apr_pvsprintf(req_xml->pool, fmt, args);
	syslog(LOG_ERR, "libvmod-soap, V_xid:%u, SOAP: %s", req_xml->ctx->sp->vxid, sei->ei.message);

	//TODO: sei->ei.synth_error = synth_soap_fault;
	if (!sei->soap_version) sei->soap_version = SOAP11;

	va_end(args);
}

/* -------------------------------------------------------------------------------------/
   Runs XPath expression against single xml node
*/
const char* evaluate_xpath(struct priv_soap_vcl *soap_vcl, struct priv_soap_task *soap_task, xmlNodePtr node, const char* xpath)
{
	xmlXPathContextPtr	    xpathCtx;
	xmlXPathObjectPtr	    xpathObj;
	struct soap_namespace	    *ns;
	int			    i;

	AN(node);
	AN(node->doc);
	VSLb(soap_task->ctx->vsl, SLT_Debug, "SOAP: xpath %s find in %s", xpath, node->name);

	xpathCtx = xmlXPathNewContext(node->doc);
	XXXAN(xpathCtx);

	VSLIST_FOREACH(ns, &soap_vcl->namespaces, list) {
		if (xmlXPathRegisterNs(xpathCtx, ns->prefix, ns->uri)) {
			VSLb(soap_task->ctx->vsl, SLT_Error, "SOAP: can't register ns (%s,%s)", ns->prefix, ns->uri);
			return ("TODO: mark ERROR");
		}
	}

	xpathCtx->node = node;
	xpathObj = xmlXPathEvalExpression(xpath, xpathCtx);
	if(xpathObj == NULL) {
		xmlXPathFreeContext(xpathCtx);
		VSLb(soap_task->ctx->vsl, SLT_Error, "SOAP: can't validate xpath %s", xpath);
		return ("TODO: mark wrong xPath");
	}

	for (i = 0; i < (xpathObj->nodesetval ? xpathObj->nodesetval->nodeNr : 0); i++) {
		if( xpathObj->nodesetval->nodeTab[i]->children &&
		    xpathObj->nodesetval->nodeTab[i]->children->content ) {
			return (xpathObj->nodesetval->nodeTab[i]->children->content);
		}
	}

	xmlXPathFreeObject(xpathObj);
	VSLb(soap_task->ctx->vsl, SLT_Debug, "SOAP: xpath %s find nothing in %s", xpath, node->name);
	return "";
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
	case 0:	 //null depth only envelope is acceptable
		if (xmlStrEqual(localname, "Envelope"))
		{
			if (xmlStrstr(URI, soap_versions[SOAP11]) != 0)
				sax_ctx->soap_version = SOAP11;
			else if (xmlStrstr(URI, soap_versions[SOAP12]) != 0)
				sax_ctx->soap_version = SOAP12;
			else
			{
				sax_ctx->error = WS_Printf(sax_ctx->ctx->ws, "Invalid SOAP Envelope's namespace <%s>.", URI);
				xmlStopParser(xml_parser);
				return;
			}
		}
		else
		{
			sax_ctx->error = WS_Printf(sax_ctx->ctx->ws, "First element must be SOAP Envelope (found <%s>).", localname);
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
			sax_ctx->error = WS_Printf(sax_ctx->ctx->ws, "Invalid XML tag <%s> found (namespace: %s).", localname, URI);
			xmlStopParser(xml_parser);
			return;
		}
		break;
	case 2:
		parent = APR_ARRAY_IDX(sax_ctx->parent_stack, level - 1, elem_info*);
		//looking for routing element - first child of soap:Body
		if (xmlStrEqual(parent->localname, "Body") && !sax_ctx->action_name ) 
		{
			sax_ctx->action_namespace = WS_Copy(sax_ctx->ctx->ws, URI, strlen(URI) + 1);
			sax_ctx->action_name = WS_Copy(sax_ctx->ctx->ws, localname, strlen(localname) + 1);
			int len = xmlStrlen(URI) - 1;
			if ( sax_ctx->action_namespace && sax_ctx->action_namespace[len] == '/') ((char*)sax_ctx->action_namespace)[len] = '\0';
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
void init_xml()
{
	xmlInitParser();
	memset(&default_sax_handler, 0, sizeof(default_sax_handler));
	memset(&soap_sax_handler, 0, sizeof(soap_sax_handler));
	xmlSAX2InitDefaultSAXHandler(&default_sax_handler, 0);
	xmlSAX2InitDefaultSAXHandler(&soap_sax_handler, 0);
	soap_sax_handler.startElementNs = &start_element_ns;
	soap_sax_handler.endElementNs = &end_element_ns;
	soap_sax_handler.error = NULL;
}

void clean_xml()
{
	xmlCleanupParser();
}

void init_req_xml(struct soap_req_xml *req_xml)
{
	AN(req_xml);
	VSLb(req_xml->ctx->vsl, SLT_Debug, "init_req_xml");

	AZ(req_xml->parser);
	req_xml->parser = xmlCreatePushParserCtxt(&soap_sax_handler, 0, 0, 0, 0);
	req_xml->parser->_private = req_xml;
	req_xml->parent_stack = apr_array_make(req_xml->pool, 16, sizeof(elem_info*));
}

void clean_req_xml(struct soap_req_xml *req_xml)
{
	AN(req_xml);
	VSLb(req_xml->ctx->vsl, SLT_Debug, "clean_req_xml");

	AN(req_xml->parser);
	xmlFreeDoc(req_xml->parser->myDoc);
	xmlFreeParserCtxt(req_xml->parser);
	req_xml->parser = NULL;
}

/* -------------------------------------------------------------------------------------/
   create v11 or v12 soap fault
*/
static xmlNodePtr create_soap_fault(xmlDocPtr doc, struct soap_error_info *info)
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
int synth_soap_fault(struct soap_req_xml *req_xml)
{
	struct soap_error_info *info = (struct soap_error_info*)req_xml->error_info;
	if (info != 0)
	{
		if (0 == info->ei.status)
			info->ei.status = 500;

		VRT_l_resp_status(req_xml->ctx, info->ei.status);
		VRT_l_resp_reason(req_xml->ctx, http_status2str(info->ei.status), vrt_magic_string_end );
		xmlDocPtr doc = xmlNewDoc("1.0");
		xmlNodePtr soap_fault = create_soap_fault(doc, info);
		xmlChar *content;
		int length;
		xmlDocDumpMemory(doc, &content, &length);
		VRT_synth_page(req_xml->ctx, content, vrt_magic_string_end);
		xmlFree(content);
		xmlFreeDoc(doc);
	}
	else
	{
		VRT_l_resp_status(req_xml->ctx, 500);
		VRT_l_resp_reason(req_xml->ctx, http_status2str(500), vrt_magic_string_end );
	}
	return 0;
}

int parse_soap_chunk(struct soap_req_xml *soap_req_xml, const char *data, int length)
{
	int err = xmlParseChunk(soap_req_xml->parser, data, length, 0);

	// real error occured
	if (soap_req_xml->error || (err && !soap_req_xml->stop))
	{
		const char* message = "Unknown Error";
		if (soap_req_xml->error) {
			message = soap_req_xml->error;
		}
		else {
			xmlErrorPtr xml_error = xmlCtxtGetLastError(soap_req_xml->parser);
			if (xml_error)
				message = xml_error->message;
		}
		add_soap_error(soap_req_xml, 500, "SOAP parsing failed: %s", message);
		VSLb(soap_req_xml->ctx->vsl, SLT_Error, "SOAP parsing failed %s", message);
		return -1;
	}
	return 0;
}
