/*
 * Copyright (c) 2019, Refinitiv
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


#define XMLSTR(x) ((const xmlChar*)x)

const xmlChar* soap_versions[] = {
	0,
	XMLSTR("http://schemas.xmlsoap.org/soap/envelope/"),
	XMLSTR("http://www.w3.org/2003/05/soap-envelope")
};

static const struct gethdr_s VGC_HDR_RESP_Content_2d_Type =
{ HDR_RESP, "\015Content-Type:"};

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
	va_list args;

	va_start(args,fmt);
	if (req_xml->error_info == NULL) {
		req_xml->error_info = WS_Alloc(req_xml->ctx->ws, sizeof(*req_xml->error_info));
		XXXAN(req_xml->error_info);
		INIT_OBJ(req_xml->error_info, SOAP_ERROR_INFO_MAGIC);
	}
	req_xml->error_info->soap_version = (req_xml->soap_version ? req_xml->soap_version : SOAP11);
	req_xml->error_info->status = status;
	req_xml->error_info->message = WS_Printf(req_xml->ctx->ws, fmt, args);
	VSLb(req_xml->ctx->vsl, SLT_Error, "%s", req_xml->error_info->message);
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
		if (xmlXPathRegisterNs(xpathCtx, XMLSTR(ns->prefix), XMLSTR(ns->uri))) {
			VSLb(soap_task->ctx->vsl, SLT_Error, "SOAP: can't register ns (%s,%s)", ns->prefix, ns->uri);
			return ("TODO: mark ERROR");
		}
	}

	xpathCtx->node = node;
	xpathObj = xmlXPathEvalExpression(XMLSTR(xpath), xpathCtx);
	if(xpathObj == NULL) {
		xmlXPathFreeContext(xpathCtx);
		VSLb(soap_task->ctx->vsl, SLT_Error, "SOAP: can't validate xpath %s", xpath);
		return ("TODO: mark wrong xPath");
	}

	for (i = 0; i < (xpathObj->nodesetval ? xpathObj->nodesetval->nodeNr : 0); i++) {
		if( xpathObj->nodesetval->nodeTab[i]->children &&
		    xpathObj->nodesetval->nodeTab[i]->children->content ) {
			return ((const char*)(xpathObj->nodesetval->nodeTab[i]->children->content));
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
		if (xmlStrEqual(localname, XMLSTR("Envelope")))
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
		if (xmlStrEqual(localname, XMLSTR("Header")) && xmlStrEqual(URI, soap_versions[sax_ctx->soap_version]))
			sax_ctx->header = xml_parser->node;
		else if (xmlStrEqual(localname, XMLSTR("Body")) && xmlStrEqual(URI, soap_versions[sax_ctx->soap_version]))
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
		if (xmlStrEqual(parent->localname, XMLSTR("Body")) && !sax_ctx->action_name )
		{
			sax_ctx->action_namespace = WS_Copy(sax_ctx->ctx->ws, URI, xmlStrlen(URI) + 1);
			sax_ctx->action_name = WS_Copy(sax_ctx->ctx->ws, localname, xmlStrlen(localname) + 1);
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
	if (xmlStrEqual(localname, XMLSTR("Header")) && xmlStrstr(URI, soap_versions[sax_ctx->soap_version]))
	{
		sax_ctx->header = xml_parser->node;
	}
	else if (xmlStrEqual(localname, XMLSTR("Body")) && xmlStrstr(URI, soap_versions[sax_ctx->soap_version]))
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

	if(req_xml->parser) {
		xmlFreeDoc(req_xml->parser->myDoc);
		xmlFreeParserCtxt(req_xml->parser);
		req_xml->parser = NULL;
	}
}

/* -------------------------------------------------------------------------------------/
   create v11 or v12 soap fault
*/
static void create_soap_fault(xmlDocPtr doc, struct soap_error_info *info)
{
	AN(info->message);

	xmlNsPtr soap_ns = xmlNewNs(NULL, soap_versions[info->soap_version], XMLSTR("soap"));
	xmlNodePtr soap_envelope = xmlNewNode(soap_ns, XMLSTR("Envelope"));
	soap_envelope->nsDef = soap_ns;
	xmlDocSetRootElement(doc, soap_envelope);

	xmlNodePtr soap_body = xmlNewNode(soap_ns, XMLSTR("Body"));
	xmlAddChild(soap_envelope, soap_body);

	xmlNodePtr soap_fault = xmlNewNode(soap_ns, XMLSTR("Fault"));
	xmlAddChild(soap_body, soap_fault);

	if (info->soap_version == SOAP12)
	{
		xmlNodePtr fault_code = xmlNewNode(soap_ns, XMLSTR("Code"));
		xmlAddChild(soap_fault, fault_code);
		xmlNewTextChild(fault_code, soap_ns, XMLSTR("Value"), XMLSTR("soap:Receiver"));

		xmlNodePtr fault_reason = xmlNewNode(soap_ns, XMLSTR("Reason"));
		xmlAddChild(soap_fault, fault_reason);
		xmlNodePtr text = xmlNewTextChild(fault_reason, soap_ns, XMLSTR("Text"), XMLSTR(info->message));
		xmlNewProp(text, XMLSTR("xml:lang"), XMLSTR("en-US"));
	}
	else
	{
		xmlNewTextChild(soap_fault, soap_ns, XMLSTR("faultcode"), XMLSTR("soap:Receiver"));
		xmlNewTextChild(soap_fault, soap_ns, XMLSTR("faultstring"), XMLSTR(info->message));
	}
}

/* -------------------------------------------------------------------------------------/
   synth SOAP fault
*/
void synth_soap_fault(struct soap_req_xml *req_xml, int code, const char* message)
{
	xmlDocPtr doc;
	xmlChar *content;
	int length;

	if (req_xml->error_info == NULL) {
		add_soap_error(req_xml, code, message);
	}
	CHECK_OBJ_NOTNULL(req_xml->error_info, SOAP_ERROR_INFO_MAGIC);

	doc = xmlNewDoc(XMLSTR("1.0"));
	create_soap_fault(doc, req_xml->error_info);
	xmlDocDumpMemory(doc, &content, &length);
	VRT_synth_page(req_xml->ctx, (const char*)content, vrt_magic_string_end);
	VRT_SetHdr(req_xml->ctx, &VGC_HDR_RESP_Content_2d_Type,
	    "application/soap+xml; charset=utf-8",
	    vrt_magic_string_end
	);
	xmlFree(content);
	xmlFreeDoc(doc);
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
