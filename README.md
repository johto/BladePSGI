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

You should now have a binary called BladePSGI, which you can run normally.

Loaders
-------

To be documented.
