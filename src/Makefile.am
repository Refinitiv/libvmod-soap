AM_CFLAGS = $(VARNISHAPI_CFLAGS) -Wall -Werror
AM_LDFLAGS = $(VARNISHAPI_LIBS) $(VMOD_LDFLAGS)

vmod_LTLIBRARIES = libvmod_soap.la

libvmod_soap_la_SOURCES = \
	vmod_soap.c

nodist_libvmod_soap_la_SOURCES = \
	vcc_soap_if.c \
	vcc_soap_if.h

dist_man_MANS = vmod_soap.3

@BUILD_VMOD_SOAP@

AM_TESTS_ENVIRONMENT = \
	PATH="$(VMOD_TEST_PATH)" \
	LD_LIBRARY_PATH="$(VARNISH_LIBRARY_PATH)"
TEST_EXTENSIONS = .vtc
VTC_LOG_COMPILER = varnishtest -v
AM_VTC_LOG_FLAGS = -Dvmod_soap="$(VMOD_SOAP)"

TESTS = @VMOD_TESTS@

EXTRA_DIST = \
	vmod_soap.vcc \
	$(VMOD_TESTS)
