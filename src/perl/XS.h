#ifndef __BLADEPSGI_INPUT_STREAM_HEADER__
#define __BLADEPSGI_INPUT_STREAM_HEADER__

typedef struct
{
	void *mainapp;
	void *worker;
} BPSGI_Context;

typedef struct
{
	void *sem;
} BPSGI_Semaphore;

/* glue functions defined in perl_interpreter.cpp */
extern void
bladepsgi_perl_interpreter_cb_set_worker_status(BPSGI_Context *ctx, const char *status);
extern int
bladepsgi_perl_interpreter_cb_fastcgi_listen_sockfd(BPSGI_Context *ctx);
extern const char *
bladepsgi_perl_interpreter_cb_psgi_application_path(BPSGI_Context *ctx);
extern const char *
bladepsgi_perl_interpreter_cb_psgi_application_loader(BPSGI_Context *ctx);
extern const char *
bladepsgi_perl_interpreter_cb_new_semaphore(BPSGI_Context *ctx, BPSGI_Semaphore *sem, const char *name, int value);
extern int
bladepsgi_perl_interpreter_cb_sem_tryacquire(BPSGI_Semaphore *sem);
extern void
bladepsgi_perl_interpreter_cb_sem_release(BPSGI_Semaphore *sem);


#endif
