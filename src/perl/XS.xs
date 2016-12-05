#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "XS.h"

MODULE = BPSGI PACKAGE=BPSGI::Context PREFIX = bladepsgi_context_
PROTOTYPES: DISABLE

void
bladepsgi_context_set_worker_status(CTX,CHR)
    BPSGI_Context *CTX
	char *CHR
    CODE:
        if (CTX->worker == NULL)
            croak("worker status change attempted from a non-worker BladePSGI context\n");
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

SV *
bladepsgi_context_new_semaphore(CTX,NAME,VALUE)
    BPSGI_Context *CTX
    char *NAME
    int VALUE
    CODE:
        BPSGI_Semaphore *sem = malloc(sizeof(BPSGI_Semaphore));
        const char *error = bladepsgi_perl_interpreter_cb_new_semaphore(CTX, sem, NAME, VALUE);
        if (error != NULL)
            croak("could not create a new semaphore %s: %s\n", NAME, error);
        RETVAL = newSViv(0);
        RETVAL = sv_setref_pv(RETVAL, "BPSGI::Semaphore", sem);
    OUTPUT:
        RETVAL

MODULE = BPSGI PACKAGE=BPSGI::Semaphore PREFIX = bladepsgi_semaphore_
PROTOTYPES: DISABLE

SV *
bladepsgi_semaphore_tryacquire(SEM)
    BPSGI_Semaphore *SEM
    CODE:
        RETVAL = boolSV(bladepsgi_perl_interpreter_cb_sem_tryacquire(SEM));
    OUTPUT:
        RETVAL

SV *
bladepsgi_semaphore_release(SEM)
    BPSGI_Semaphore *SEM
    CODE:
        bladepsgi_perl_interpreter_cb_sem_release(SEM);
