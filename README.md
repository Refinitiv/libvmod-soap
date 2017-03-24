libvmod-soap [![Build Status](https://travis-ci.org/thomsonreuters/libvmod-soap.svg?branch=master)](https://travis-ci.org/thomsonreuters/libvmod-soap)
=============


SOAP VMOD compatible with Varnish 4 and 5.

``libvmod-soap`` reads SOAP XML basic elements in HTTP request body (by using ``action``, ``uri``, and/or  ``xpath``). It allows users to use VCL with these SOAP values.

Usage and Examples
=============
For a given SOAP XML message stored in Request's body :
```xml
<?xml version="1.0" encoding="utf-8"?>
<Envelope xmlns="http://schemas.xmlsoap.org/soap/envelope/"><Body>
<Login xmlns="http://website.com/auth">
    <Username xmlns="http://website.com/user">JohnDoe</Username>
    <Password xmlns="http://website.com/user">foobar</Password>
</Login>
</Body></Envelope>}
```

Select backend accordingly to the value of SOAP action:
```vcl
import soap;
sub vcl_recv {
    if(soap.action() == "Login") {
        set req.backend_hint = soap_login;
    }
}
```

Verify action namespace:
```vcl
import soap;
sub vcl_recv {
    if(soap.action_namespace() !~ "^http://website.com") {
        return (soap.synth(400, "Bad SOAP namespace"));
    }
}
```

Search XPath values and put it into HTTP headers
```vcl
import soap;
sub vcl_init {
        soap.add_namespace("a", "http://website.com/auth");
        soap.add_namespace("u", "http://website.com/user");
}
sub vcl_recv {
        set req.http.user-id = soap.xpath_body("a:Login/u:User");
        set req.http.user-pwd = soap.xpath_body("a:Login/u:Password");
}
```

Looking for more ? See other examples on https://github.com/thomsonreuters/libvmod-soap/tree/master/src/tests. 

VMOD Interface
=============

```
is_valid()
```
Returns TRUE if the request body is a valid SOAP message.

```
action()
```
Returns name of SOAP body's action

```
action_namespace()
```
Returns namespace of SOAP body's action

```
add_namespace(prefix, uri)
```
Add namespace "prefix/uri" into known namespaces.
See also `xpath_header` and `xpath_body`.

```
xpath_header(xpath_pattern):
```
Returns xpath value in SOAP Header.
It uses the namespace table (see `add_namespace`).

```
xpath_body(xpath_pattern)
```
Returns xpath value in SOAP Body.
It uses the namespace table (see `add_namespace`).


```
VOID synthetic(faultcode, faultmessage)
```
Creates a SOAP synthetic response's body containing SOAP FaultCode and 
FaultMessage. Note that FaultMessage contains any internal errors found.
Format of the response depends of the SOAP version of the request (either 1.1 or 1.2).


Copyright
=============
This document is licensed under BSD-2-Clause license. See LICENSE for details.

That code has been opened by (c) Thomson Reuters.
