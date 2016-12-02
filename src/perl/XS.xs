#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "XS.h"

MODULE = BLADEPSGI PACKAGE=BLADEPSGI::Context PREFIX = bladepsgi_context_
PROTOTYPES: DISABLE

void
bladepsgi_context_set_worker_status(CTX,CHR)
    BPSGI_Context *CTX
	char *CHR
    CODE:
        if (CTX->worker == NULL)
            croak("worker status change attempted from a non-worker BLADEPSGI context\n");
		bladepsgi_perl_interpreter_cb_set_worker_status(CTX, CHR);

SV *
bladepsgi_context_fastcgi_listen_sockfd(CTX)
    BPSGI_Context *CTX
    CODE:
        RETVAL = newSViv(bladepsgi_perl_interpreter_cb_fastcgi_listen_sockfd(CTX));
    OUTPUT:
        RETVAL

SV *
bladepsgi_context_psgi_application_path(CTX)
    BPSGI_Context *CTX
    CODE:
        RETVAL = newSVpv(bladepsgi_perl_interpreter_cb_psgi_application_path(CTX), 0);
    OUTPUT:
        RETVAL

SV *
bladepsgi_context_psgi_application_loader(CTX)
    BPSGI_Context *CTX
    CODE:
        RETVAL = newSVpv(bladepsgi_perl_interpreter_cb_psgi_application_loader(CTX), 0);
    OUTPUT:
        RETVAL

SV *
bladepsgi_context_is_worker(CTX)
    BPSGI_Context *CTX
    CODE:
        RETVAL = boolSV(CTX->worker != NULL);
    OUTPUT:
        RETVAL
