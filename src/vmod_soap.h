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
#include <stdlib.h>

#include "vmod_soap_http.h"

#define BUFFER_SIZE 8192

#define HANDLER_ERROR -1
#define HANDLER_SUCCESS_NOT_MODIFIED 0
#define HANDLER_SUCCESS_MODIFIED 1
#define HANDLER_SUCCESS_NEEDBODY 2

#define SOAP11 1
#define SOAP12 2

struct sess_record;
typedef struct _error_info {
    int status;
    const char *message;
    int (*synth_error)(struct sess_record *r);
} error_info;

typedef struct _soap_error_info
{
    error_info ei;
    int soap_version;
} soap_error_info;

typedef struct body_part {
    char *data;
    int length;
} body_part;

typedef struct _http_context {
    struct http_conn *htc;      // Varnish internal structure
    enum ce_type compression;   // HTTP compression
    apr_pool_t *pool;           // Memory pool
    apr_array_header_t *body;   // HTTP payload stored as is
    z_stream *compression_stream;
} http_context;

enum processing_type{
    REST = 1,
    SOAP  = 2
};

// this one is called when first element of the body if found, 
// even if no headers are available - header in this case will be NULL
typedef int (*headers_parsed) (const char* name, const char* ns, xmlNodePtr header, struct sess_record *s);
// this one is called when whole <soap:Body> element is parsed
typedef int (*body_available) (const char* name, const char* ns, xmlNodePtr header, xmlNodePtr body, struct sess_record *s);

typedef struct _soapparse_cb {
    int soap_version;           //      SOAP version is available here;
} soapparse_cb;    

typedef struct sess sess;
struct _error_info;

struct soap_namespace {
        unsigned                        magic;
#define PRIV_SOAP_NAMESPACE_MAGIC 0x5FFBCA91
        const char* name;
        const char* uri;
	VSLIST_ENTRY(soap_namespace)	list;
};

struct priv_soap_vcl {
        unsigned                        magic;
#define PRIV_SOAP_VCL_MAGIC 0x5FF42842
	VSLIST_HEAD(, soap_namespace)   namespaces;
};

typedef struct priv_soap_task {
        unsigned magic;
#define PRIV_SOAP_TASK_MAGIC 0x5FF52A40
        VRT_CTX;
        apr_pool_t* pool;
        const char* action;
        const char* action_namespace;
        void* header;
        struct _error_info *error_info;
} sess_record;

extern apr_pool_t *s_module_pool;
extern apr_hash_t* s_module_storage;
extern apr_thread_mutex_t* s_module_mutex;
// Common HTTP Headers
static const char szContentLength[] = "\017Content-Length:";
static const char szContentEncoding[] = "\021Content-Encoding:";

static const struct gethdr_s VGC_HDR_REQ_Content_2d_Length = { HDR_REQ, szContentLength };
static const struct gethdr_s VGC_HDR_BERESP_Content_2d_Length = { HDR_BERESP, szContentLength };

#include "vmod_soap_gzip.h"
#include "vmod_soap_xml.h"
#include "vmod_soap_request.h"

#endif
