#include "vmod_soap.h"
#include "vmod_soap_http.h"
#include "vmod_soap_gzip.h"

/* -------------------------------------------------------------------------------------/
    Init HTTP context with certain compression type
*/
int init_http_context(http_context **ctx, enum ce_type compression ,apr_pool_t *pool)
{
    int sts = 1;
    *ctx = (http_context*)apr_palloc(pool, sizeof(http_context));
    if(*ctx == NULL)
          return sts;
    (*ctx)->body = apr_array_make(pool, 16, sizeof(body_part*));
    if( (*ctx)->body == NULL)
          return sts;
    (*ctx)->pool = pool;
    (*ctx)->compression = compression;
    int init_flags = MAX_WBITS;
    switch (compression)
    {
        case CE_GZIP:
            init_flags += 16;
        case CE_DEFLATE:
            (*ctx)->compression_stream = (z_stream*)apr_pcalloc(pool, sizeof(z_stream));
            sts = inflateInit2((*ctx)->compression_stream, init_flags);
            break;
        default: 
            sts = 0;
            break;
    }    
    return sts;
}

/* -------------------------------------------------------------------------------------/
    HTTP context cleanup
*/
int uninit_http_context(http_context **ctx)
{
    if (*ctx == 0)
    {
        return 0;
    }
    if ((*ctx)->compression != CE_NONE)
    {
        inflateEnd((*ctx)->compression_stream);
    }
    *ctx = 0;
    return 0;
}

/* -------------------------------------------------------------------------------------/
    Decompress one part
*/
int uncompress_body_part(z_stream *stream, body_part *compressed_body_part, body_part *uncompressed_body_part, apr_pool_t *pool)
{
    Bytef buf[BUFFER_SIZE*8];
    Bytef *res_buf = 0;
    int res_len = 0;
    int sts = 0;
    stream->next_in = (Bytef *)compressed_body_part->data;
    stream->avail_in = compressed_body_part->length;
    while(stream->avail_in > 0)
    {
        stream->next_out = buf;
        stream->avail_out = BUFFER_SIZE*8;
        int err = inflate(stream, Z_SYNC_FLUSH);
        if (err != Z_OK && err != Z_STREAM_END)
        {
            sts = 1;
            break;
        }
        Bytef *new_buf = (Bytef*) malloc(stream->total_out);
        memcpy(new_buf, res_buf, res_len);
        memcpy(new_buf + res_len, buf, stream->next_out - buf);
        if (res_buf) free(res_buf);
        res_buf = new_buf;
        res_len += stream->next_out - buf;
    }
    uncompressed_body_part->data = (char*)apr_pmemdup(pool, res_buf, res_len);
    uncompressed_body_part->length = res_len;
    free(res_buf);
    return sts;
}
