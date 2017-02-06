#include <syslog.h>
#include "vmod_soap.h"

#define POOL_KEY "VRN_IH_PK"

static pthread_mutex_t  soap_mutex = PTHREAD_MUTEX_INITIALIZER;
static int              refcount = 0;
static apr_pool_t       *apr_pool = NULL;

/* -------------------------------------------------------------------------------------/
    init module
*/
static void init_apr()
{
    AZ(apr_pool);
    XXXAZ(apr_initialize());
    XXXAZ(apr_pool_create(&apr_pool, NULL));
}

static void clean_apr()
{
    apr_pool = NULL;
    apr_terminate();
}

/* -------------------------------------------------------------------------------------/
    init vcl
*/
static void clean_vcl(void *priv)
{
    struct priv_soap_vcl *priv_soap_vcl;
    struct soap_namespace *ns, *ns2;

    CAST_OBJ_NOTNULL(priv_soap_vcl, priv, PRIV_SOAP_VCL_MAGIC);

    VSLIST_FOREACH_SAFE(ns, &priv_soap_vcl->namespaces, list, ns2) {
        VSLIST_REMOVE_HEAD(&priv_soap_vcl->namespaces, list);
        FREE_OBJ(ns);
    }

    FREE_OBJ(priv_soap_vcl);
}

static struct priv_soap_vcl* init_vcl()
{
    struct priv_soap_vcl *priv_soap_vcl;

    ALLOC_OBJ(priv_soap_vcl, PRIV_SOAP_VCL_MAGIC);
    XXXAN(priv_soap_vcl);

    VSLIST_INIT(&priv_soap_vcl->namespaces);
    return priv_soap_vcl;
}

/* ------------------------------------------------------------------/
    initialize session
*/
static struct priv_soap_task* init_task(VRT_CTX)
{
    struct priv_soap_task *priv_soap_task;

    ALLOC_OBJ(priv_soap_task, PRIV_SOAP_TASK_MAGIC);
    AN(priv_soap_task);

    XXXAZ(apr_pool_create(&priv_soap_task->pool, apr_pool));
    priv_soap_task->ctx = ctx;
    priv_soap_task->action = NULL;
    priv_soap_task->action_namespace = NULL;
    return priv_soap_task;
}

/* -----------------------------------------------------------------/
    destroy session
*/
static void clean_task(void *priv)
{
   struct priv_soap_task *priv_soap_task;

   AN(priv);
   CAST_OBJ_NOTNULL(priv_soap_task, priv, PRIV_SOAP_TASK_MAGIC);

   AN(priv_soap_task->pool);
   apr_pool_destroy(priv_soap_task->pool);

   FREE_OBJ(priv_soap_task);
}

/*
 * handle vmod internal state, vmod init/fini and/or varnish callback
 * (un)registration here.
 *
 */
int __match_proto__(vmod_event_f)
event_function(VRT_CTX, struct vmod_priv *priv /* PRIV_VCL */, enum vcl_event_e e)
{
    struct priv_soap_vcl *priv_soap_vcl;

    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

    switch (e) {
    case VCL_EVENT_LOAD:
	AZ(pthread_mutex_lock(&soap_mutex));
        if(0 == refcount++) {
            init_xml();
            init_apr();
        }
	AZ(pthread_mutex_unlock(&soap_mutex));

        priv_soap_vcl = init_vcl();
        priv->priv = priv_soap_vcl;
        priv->free = clean_vcl;
        break;
    case VCL_EVENT_WARM:
        break;
    case VCL_EVENT_COLD:
        break;
    case VCL_EVENT_DISCARD:
	AZ(pthread_mutex_lock(&soap_mutex));
        if(0 == --refcount) {
            clean_xml();
            clean_apr();
        }
	AZ(pthread_mutex_unlock(&soap_mutex));
        break;
    default:
        return (0);
    }
    return (0);
}

sess_record* priv_soap_get(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
        struct priv_soap_task *priv_soap_task;

        CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
        AN(priv);
        if(priv->priv == NULL) {
            priv->priv = init_task(ctx);
            priv->free = clean_task;
        }
        CAST_OBJ_NOTNULL(priv_soap_task, priv->priv, PRIV_SOAP_TASK_MAGIC);
        return (priv_soap_task);
}

VCL_STRING vmod_action(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
        struct priv_soap_task *r = priv_soap_get(ctx, priv);
        if(process_soap_request(r) == 0) {
                return r->action;
        }
        return "TODO: ERROR";
}

VCL_STRING vmod_action_namespace(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
        struct priv_soap_task *r = priv_soap_get(ctx, priv);
        if(process_soap_request(r) == 0) {
                return r->action_namespace;
        }
        return "TODO: ERROR";
}

void vmod_add_namespace(VRT_CTX, struct vmod_priv *priv /* PRIV_VCL */, const char* name, const char* uri)
{
    struct priv_soap_vcl        *priv_soap_vcl;
    struct soap_namespace       *namespace;

    AN(priv);
    CAST_OBJ_NOTNULL(priv_soap_vcl, priv->priv, PRIV_SOAP_VCL_MAGIC);
    ALLOC_OBJ(namespace, PRIV_SOAP_NAMESPACE_MAGIC);
    AN(namespace);

    namespace->name = name;
    namespace->uri = uri;
    VSLIST_INSERT_HEAD(&priv_soap_vcl->namespaces, namespace, list);
}
