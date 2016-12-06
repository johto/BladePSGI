#ifndef __BLADEPSGI_PERL_INTERPRETER_HEADER__
#define __BLADEPSGI_PERL_INTERPRETER_HEADER__

#include "XS.h"

struct bladepsgi_perl_callback_t {
	void *bladepsgictx;
	void *sv;
};

struct bladepsgi_psgi_application_t {
	struct bladepsgi_perl_callback_t *callback;
};

extern void bladepsgi_perl_per_process_init(void);
extern int bladepsgi_perl_interpreter_init(char **error_out);
extern int bladepsgi_perl_interpreter_destroy(char **error_out);
extern int bladepsgi_psgi_application_init(const char *path, char **error_out);

extern int bladepsgi_perl_callback_init(void *bladepsgictx,
										const char *source,
										const char *loader,
										struct bladepsgi_perl_callback_t **cbs_out,
										char **error_out);
extern int bladepsgi_perl_callback_call(struct bladepsgi_perl_callback_t *cbs, char **error_out);
extern int bladepsgi_perl_callback_call_and_receive_callback(struct bladepsgi_perl_callback_t *cbs,
															 char **error_out,
															 struct bladepsgi_perl_callback_t **cbs_out);

#endif
