libvmod-soap [![Build Status](https://travis-ci.org/thomsonreuters/libvmod-soap.svg?branch=master)](https://travis-ci.org/thomsonreuters/libvmod-soap)
=============


SOAP VMOD compatible with Varnish 4 and 5.

``libvmod-soap`` reads SOAP XML basic elements in HTTP request body (by using ``action``, ``uri``, and/or  ``xpath``). It allows users to use VCL with these SOAP values.

Functions
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

Examples
=============

Select backend accordingly to the value of SOAP action in a request POST's body:

Request's body :
```
<?xml version="1.0" encoding="utf-8"?>
<Envelope xmlns="http://schemas.xmlsoap.org/soap/envelope/"><Body>
<Login/>
</Body></Envelope>}
```
Varnish VCL code :
```
import soap;
sub vcl_recv {
    if(soap.action() == "Login") {
        set req.backend_hint = soap_login;
    }
}
```

Copyright
=============
This document is licensed under BSD-2-Clause license. See LICENSE for details.

That code has been opened by (c) Thomson Reuters.
