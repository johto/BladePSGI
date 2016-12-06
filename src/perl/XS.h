#ifndef __BLADEPSGI_INPUT_STREAM_HEADER__
#define __BLADEPSGI_INPUT_STREAM_HEADER__

#include <stdint.h>

typedef struct
{
	void *mainapp;
	void *worker;
} BPSGI_Context;

typedef struct
{
	void *sem;
} BPSGI_Semaphore;

typedef int64_t BPSGI_AtomicInt64;

/* glue functions defined in perl_interpreter_sea_bridge.cpp */
extern void
bladepsgi_perl_interpreter_cb_set_worker_status(BPSGI_Context *ctx, const char *status);
extern int
bladepsgi_perl_interpreter_cb_fastcgi_listen_sockfd(BPSGI_Context *ctx);
extern const char *
bladepsgi_perl_interpreter_cb_psgi_application_path(BPSGI_Context *ctx);
extern const char *
bladepsgi_perl_interpreter_cb_psgi_application_loader(BPSGI_Context *ctx);
extern const char *
bladepsgi_perl_interpreter_cb_request_auxiliary_process(BPSGI_Context *ctx, const char *name, void *sv);
extern const char *
bladepsgi_perl_interpreter_cb_new_semaphore(BPSGI_Context *ctx, BPSGI_Semaphore *sem, const char *name, int value);
extern const char *
bladepsgi_perl_interpreter_cb_new_atomic_int64(BPSGI_Context *ctx, BPSGI_AtomicInt64 **atm, const char *name, int value);
extern int
bladepsgi_perl_interpreter_cb_sem_tryacquire(BPSGI_Semaphore *sem);
extern void
bladepsgi_perl_interpreter_cb_sem_release(BPSGI_Semaphore *sem);
extern int64_t
bladepsgi_perl_interpreter_cb_atomic_int64_fetch_add(BPSGI_AtomicInt64 *atm, int64_t value);
extern int64_t
bladepsgi_perl_interpreter_cb_atomic_int64_load(BPSGI_AtomicInt64 *atm);
extern void
bladepsgi_perl_interpreter_cb_atomic_int64_store(BPSGI_AtomicInt64 *atm, int64_t value);


#endif
