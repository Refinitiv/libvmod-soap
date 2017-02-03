#include <syslog.h>
#include "vmod_soap.h"

#define POOL_KEY "VRN_IH_PK"

static const char* s_init_error = "n/a";
static int module_inited = 0;
apr_pool_t* s_module_pool = 0;
apr_hash_t* s_module_storage = NULL;
apr_thread_mutex_t* s_module_mutex = NULL;

/* -------------------------------------------------------------------------------------/
    init wsg
*/
#define init_r(r) routine=#r; if ((status = r)) goto E_x_i_t_;
int init_wsg_module()
{
    apr_status_t status = APR_SUCCESS;
    const char* routine = "n/a";

    // Initialize apr, main pool and storage
    if (APR_SUCCESS ==(status  = apr_initialize()))
        if (APR_SUCCESS ==(status = apr_pool_create(&s_module_pool, NULL)))
            if (NULL != (s_module_storage = apr_hash_make(s_module_pool)))
                apr_thread_mutex_create(&s_module_mutex, APR_THREAD_MUTEX_DEFAULT, s_module_pool);

    if (s_module_mutex == NULL)
    {
        s_init_error = "Failed to initialize APR or main pool";
        if (!status) status = APR_EGENERAL;
        goto E_x_i_t_;
    }

    xmlInitParser();

    init_r(init_soap_sax_handler());

E_x_i_t_:
    if (status) {
        syslog(LOG_EMERG, "WSG module initialization failed: routine:'%s',rc=%d,message:'%s'", routine, status, s_init_error); 
    }
    return status;
}

/* -------------------------------------------------------------------------------------/
    initialize session
*/
static sess_record* init_sess_rec(VRT_CTX)
{
   void* key = 0;
   apr_pool_t* pool = NULL;
   sess_record* rec = NULL;
   apr_status_t status = APR_EINIT;

   if (!ctx)
      goto E_x_i_t_;

   apr_thread_mutex_lock(s_module_mutex);

   status = apr_pool_create(&pool, s_module_pool);

   if (APR_SUCCESS != status) // failed to create pool
      goto E_x_i_t_;

   rec = (sess_record*)apr_pcalloc(pool, sizeof(sess_record));

   if (!rec)
      goto E_x_i_t_;

   rec->pool = pool;
   rec->ctx = ctx;

   if (NULL == (rec->aggregated_context = apr_hash_make(rec->pool)))
      goto E_x_i_t_;

   status = apr_pool_userdata_setn(rec, POOL_KEY, NULL, pool);

   if (APR_SUCCESS != status)
      goto E_x_i_t_;

   key = apr_palloc(pool, sizeof(ctx));
   if (NULL == key)
    goto E_x_i_t_;

   memcpy(key, &ctx, sizeof(ctx));
   apr_hash_set(s_module_storage, key, sizeof(ctx), pool);

   status = APR_SUCCESS;
    
   goto E_x_i_t_1;
   
E_x_i_t_:
   if (NULL != pool)
      apr_pool_destroy(pool);

E_x_i_t_1:
   apr_thread_mutex_unlock(s_module_mutex);
   return rec;
}


/* -------------------------------------------------------------------------------------/
    get current session object
*/
static sess_record* get_sess_rec(const sess* s)
{
   apr_pool_t* pool = NULL;
   sess_record* rec = NULL;

   apr_thread_mutex_lock(s_module_mutex);

   pool = (apr_pool_t*)apr_hash_get(s_module_storage, &s, sizeof(sess*));

   apr_thread_mutex_unlock(s_module_mutex);

   if (NULL != pool)
      apr_pool_userdata_get((void**)&rec, POOL_KEY, pool);

   return rec;
}

/* -------------------------------------------------------------------------------------/
    destroy session
*/
static void destroy_sess(const sess* s)
{
   apr_pool_t* pool = NULL;
   apr_thread_mutex_lock(s_module_mutex);

   pool = (apr_pool_t*)apr_hash_get(s_module_storage, &s, sizeof(s));

   if(NULL != pool) {
      apr_hash_set(s_module_storage, &s, sizeof(s), NULL);
      apr_pool_destroy(pool);
   }

   apr_thread_mutex_unlock(s_module_mutex);
}

/*
 * handle vmod internal state, vmod init/fini and/or varnish callback
 * (un)registration here.
 *
 */
int __match_proto__(vmod_event_f)
event_function(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{
    switch (e) {
    case VCL_EVENT_LOAD:
        if(module_inited++ == 0) {
            return init_wsg_module();
        }
        break;
    case VCL_EVENT_WARM:
        break;
    case VCL_EVENT_COLD:
        break;
    case VCL_EVENT_DISCARD:
        if(--module_inited == 0) {
            xmlCleanupParser();
            apr_terminate();
        }
        break;
    default:
        return (0);
    }
    return (0);
}

struct vmod_soap {
	unsigned magic;
#define VMOD_SOAP_MAGIC 0x5FF42842
        
};
/*
static void
soap_free(void *p)
{
	struct vmod_soap *soap;

	CAST_OBJ_NOTNULL(soap, p, VMOD_SOAP_MAGIC);
	FREE_OBJ(soap);
}

static struct vmod_soap *
soap_get(struct vmod_priv *priv)
{
	struct vmod_soap *soap;

        AN(priv);
        get_sess_rec();
        init_sess_rec();
        destroy_sess();
	if (priv->priv == NULL) {
		ALLOC_OBJ(soap, VMOD_SOAP_MAGIC);
		AN(soap);
		priv->priv = soap;
		priv->free = soap_free;
	} else {
		CAST_OBJ_NOTNULL(soap, priv->priv, VMOD_SOAP_MAGIC);
        }

	return (soap);
}
*/
/* VCL_STRING */
/* vmod_read_action(VRT_CTX, struct vmod_priv *priv /\* PRIV_TASK *\/) */
/* { */
/*         struct vmod_soap * soap = soap_get(priv); */

/* 	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC); */
/*         if(soap->request == NULL) { */
/*                 soap_read_request(ctx, soap) */
/*         } */
/* 	return (p); */
/* } */

VCL_STRING vmod_read_action(VRT_CTX, struct vmod_priv *priv /* PRIV_TASK */)
{
    /* sess_record *r = init_sess_rec(ctx); */
    /* process_soap_request(r); */
    return "toto";
}
