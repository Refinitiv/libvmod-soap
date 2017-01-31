#include <string.h>
#include "cache/cache.h"
#include "vmod_soap_http.h"

/* -------------------------------------------------------------------------------------/
    Returns the individual char value of the numeric status codes, according to RFC 2616
*/

const char* http_status2str(const int status)
{
    switch (status)
    {
        default: return "Internal Server Error";
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 306: return "(Unused)";
        case 307: return "Temporary Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Request Entity Too Large";
        case 414: return "Request-URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Requested Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
    }
    return "Unknown";
}

int http_content_encoding(struct http *http)
{
    const char *ptr;
    if (http_GetHdr(http, H_Content_Encoding, &ptr))
    {
        if (strstr(ptr, "gzip") != 0)
        {
            return CE_GZIP;
        }
        else if (strstr(ptr, "deflate") != 0)
        {
            return CE_DEFLATE;
        }
        else
        {
            return CE_UNKNOWN;
        }
    }
    return CE_NONE;
}
