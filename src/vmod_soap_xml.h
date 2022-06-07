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

#ifndef __VMOD_SOAP_XML_H__
#define __VMOD_SOAP_XML_H__

#include <libxml/tree.h>
#include <libxml/parser.h>

struct sess_record;

struct soap_error_info
{
	unsigned		magic;
#define SOAP_ERROR_INFO_MAGIC 0x5EAB1BC1
	int			soap_version;
	int			status;
	const char		*message;
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

void synth_soap_fault(struct soap_req_xml *req_xml, int code, const char* message);
void init_xml();
void clean_xml();
void init_req_xml(struct soap_req_xml *req_xml);
void clean_req_xml(struct soap_req_xml *req_xml);

int v_matchproto_(objiterate_f)
soap_iter_f(void *priv, unsigned flush, const void *ptr, ssize_t len);
int test_ns(VCL_STRING prefix, VCL_STRING uri);
const char* evaluate_xpath(struct priv_soap_vcl *soap_vcl, struct priv_soap_task *soap_task, xmlNodePtr node, const char* xpath);

#endif
