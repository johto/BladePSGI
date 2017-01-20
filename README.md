BladePSGI
=========

Introduction
------------

While [PSGI](http://plackperl.org) is a nice interface for writing web
applications, the options available for actually running these applications are
somewhat scarce.  _BladePSGI_ was developed when it become apparent that
patching our custom Perl-based runner was a dead end.

_BladePSGI_ has a process-based architecture, meaning that each instance of the
PSGI application runs in its own, single-threaded process.  _BladePSGI_ takes
care of forking all backends at startup, and then sits in the background making
sure that none of the backends crash, or shutting them down if the server
administrator asks for that to happen.  It also starts a separate monitoring
process which can be used to provide statistics to a separate monitoring
system, e.g. Prometheus.

For communication with other services, _BladePSGI_ exposes a UNIX domain
FastCGI socket.  Any (for example) HTTP daemon capable of acting as a FastCGI
client can be used to then expose the application to the internet.

If the PSGI application author chooses they can ask for "auxiliary processes"
to be started and run with the application, expose the status of each backend
process in shared memory, or share atomics-based semaphores or atomic integers
between PSGI application backends.

Requirements
------------

The only supported platforms are OS X and Linux.  Windows support seems
unlikely, but adding support for BSDs should be relatively straightforward.

Building the binary requires cmake version 2.8 or later and a C++11 compatible
compiler.

How to build
------------

```
git clone https://github.com/johto/BladePSGI.git
cd BladePSGI
cmake .
make
```

You should now have a binary called "bladepsgi", which you can run normally.

Loaders
-------

The command-line argument --loader can be used to specify a Perl module
callback which is called once in the spawner process before the backends are
forked.  The only passed argument is an object which can be used to ask the
runner for different facilities, documented below.  The loader subroutine
should return two values: a hashref which will be merged into the PSGI
environment on every call to the PSGI application, and a subroutine for the
PSGI application itself.

The passed-in object has the following methods:

##### set\_worker\_status(status)

Expects a single octet as an argument, which will be the new status of the
current backend process.  This method should only be called from the PSGI
application, and never from the loader!

##### psgi\_application\_path()

The APPLICATION\_PATH passed to _BladePSGI_ on the command line.  Only useful
for knowing where to load the application from in the loader subroutine.

##### request\_auxiliary\_process(name, subr)

Requests an auxiliary subprocess with the provided name to be started that
lives with the application backends.  The process, once started, calls the
provided subroutine, which should never return.

##### new\_semaphore(name, initvalue)

Requests a new shared semaphore with the provided name and initial value.  The
return value is an object which provides the following methods:

    tryacquire(): If the current value of the semaphore is larger than zero,
      decreases the value by one and returns TRUE.  Otherwise returns FALSE.

    release(): Increases the current value of the semaphore by one.

##### new\_atomic\_int64(name, initvalue)

Requests a new shared atomic 64-bit integer with the provided name and initial
value.  The return value is an object which provides the following methods:

  incr(): Increases the current value by one.

  fetch\_add(val): Increases the current value by _val_ and returns the value
  before the operation.

  load(): Returns the current value.

  store(val): Stores the provided value into the integer.

Loader example
--------------

Your final loader code might look something like the following:

```Perl
package MyBladePSGILoader;

use strict;
use warnings;

use Plack::Util;

my $loader = sub {
    my $bladepsgi = shift;

    my $handle = MyAPIHandle->new();
    $handle->load_configuration_file();
    my $worker_status_setter = sub {
        $bladepsgi->set_worker_status($_[0]);
    };
    $handle->set_worker_status_setter($worker_status_setter);
    $handle->set_db_deadlock_counter($bladepsgi->new_atomic_int64('db_deadlock_counter', 0));

    my $env = {
        'myapix.handle' => $handle,
    };
    return $env, Plack::Util::load_psgi($bladepsgi->psgi_application_path());
};
```
