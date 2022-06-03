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

#ifndef __VMOD_SOAP__H__
#define __VMOD_SOAP__H__

#include <apr_general.h>
#include <apr_tables.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_strings.h>

#include <lib/libvgz/vgz.h>
#include <libxml/tree.h>
#include <libxml/parser.h>

#include <stdio.h>

#include "vmod_soap_http.h"

#define HANDLER_ERROR -1
#define HANDLER_SUCCESS_NOT_MODIFIED 0
#define HANDLER_SUCCESS_MODIFIED 1
#define HANDLER_SUCCESS_NEEDBODY 2

#define SOAP11 1
#define SOAP12 2

struct soap_req_http;
struct soap_req_xml;

struct soap_namespace {
	unsigned			magic;
#define PRIV_SOAP_NAMESPACE_MAGIC 0x5FFBCA91
	const char* prefix;
	const char* uri;
	VSLIST_ENTRY(soap_namespace)	list;
};

struct priv_soap_vcl {
	unsigned			magic;
#define PRIV_SOAP_VCL_MAGIC 0x5FF42842
	VSLIST_HEAD(, soap_namespace)	namespaces;
};

typedef struct priv_soap_task {
	unsigned			magic;
#define PRIV_SOAP_TASK_MAGIC 0x5FF52A40
	VRT_CTX;
	apr_pool_t			*pool;
	struct soap_req_http		*req_http;
	struct soap_req_xml		*req_xml;
	int				state;
	ssize_t				bytes_read;
	ssize_t				bytes_total;
} sess_record;

#include "vmod_soap_request.h"
#include "vmod_soap_gzip.h"
#include "vmod_soap_xml.h"

#endif
