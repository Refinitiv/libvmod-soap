#ifndef __VMOD_SOAP__H__
#define __VMOD_SOAP__H__

/* need vcl.h before vrt.h for vmod_evet_f typedef */
#include "vcl.h"
#include "vrt.h"
#include "cache/cache.h"
#include "cache/cache_director.h"
#include "cache/cache_backend.h"
#include "vtim.h"
#include "vcc_soap_if.h"
#include <vrt_obj.h>

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

#define BUFFER_SIZE 8192

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
	int				bytes_left;
} sess_record;

// Common HTTP Headers
static const char szContentLength[] = "\017Content-Length:";
static const char szContentEncoding[] = "\021Content-Encoding:";

static const struct gethdr_s VGC_HDR_REQ_Content_2d_Length = { HDR_REQ, szContentLength };
static const struct gethdr_s VGC_HDR_BERESP_Content_2d_Length = { HDR_BERESP, szContentLength };

#include "vmod_soap_request.h"
#include "vmod_soap_gzip.h"
#include "vmod_soap_xml.h"

#endif
