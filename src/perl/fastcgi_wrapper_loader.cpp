const char *fastcgi_wrapper_loader = R"(
package FastCGIWrapperLoader;

use strict;
use warnings;

use FCGI;
use Plack::Util;
use HTTP::Status;

sub {
	my $bladepsgi = shift;

	my ($psgi_env, $psgi_app) = undef;

	my $psgi_application_loader_module = $bladepsgi->psgi_application_loader();
	if (defined($psgi_application_loader_module)) {
		# From This::That to This/That.pm
		s/::/\//g, s/$/.pm/ for $psgi_application_loader_module;

		require $psgi_application_loader_module;

		my $psgi_application_loader_path = $INC{$psgi_application_loader_module};
		if (!defined($psgi_application_loader_path)) {
			die "internal error: required module $psgi_application_loader_module not present in %INC";
		}

		my $psgi_app_loader = do $psgi_application_loader_path;
		if (!defined($psgi_app_loader)) {
			my $ex = $@;
			if ($ex eq '') {
				$ex = "$!";
			}
			die sprintf("could not load PSGI application loader %s: %s\n",
						$psgi_application_loader_path, $ex);
		}
		if (ref($psgi_app_loader) ne 'CODE') {
			die sprintf("PSGI application loader %s must return a subroutine; got %s\n",
						$psgi_application_loader_path,
						ref($psgi_app_loader));
		}

		($psgi_env, $psgi_app) = $psgi_app_loader->($bladepsgi);
		if (ref($psgi_env) ne 'HASH') {
			die "the first returned value from PSGI application loader must be a HASH reference; got ".ref($psgi_env)."\n";
		} elsif (ref($psgi_app) ne 'CODE') {
			die "the second returned value from PSGI application loader must be a CODE reference; got ".ref($psgi_app)."\n";
		}
	} else {
		die "loader currently required, sorry";
	}

	my $sockfd = $bladepsgi->fastcgi_listen_sockfd();

	my %env;
	my ($stdin, $stdout, $stderr) = (IO::Handle->new, IO::Handle->new, IO::Handle->new);
	my $req = FCGI::Request($stdin, $stdout, $stderr, \%env, $sockfd, FCGI::FAIL_ACCEPT_ON_INTR);

	my $handle_response = sub {
		my $res = shift;

		$stdout->autoflush(1);
		binmode($stdout);

		my $hdrs;
		my $message = HTTP::Status::status_message($res->[0]);
		$hdrs = "Status: $res->[0] $message\015\012";

		my $headers = $res->[1];
		while (my ($k, $v) = splice @$headers, 0, 2) {
			$hdrs .= "$k: $v\015\012";
		}
		$hdrs .= "\015\012";

		print { $stdout } $hdrs;

		my $cb = sub { print { $stdout } $_[0] };
		my $body = $res->[2];
		if (defined $body) {
			Plack::Util::foreach($body, $cb);
		} else {
			return Plack::Util::inline_object(
				write => $cb,
				close => sub { },
			);
		}
	};

	return sub {
		if ($req->Accept() < 0) {
			return -1;
		}

		my $env = {
			%env,
			%$psgi_env,

			'psgi.version'	  => [1,1],
			'psgi.url_scheme'   => ($env{HTTPS}||'off') =~ /^(?:on|1)$/i ? 'https' : 'http',
			'psgi.input'		=> $stdin,
			# N.B: we intentionally don't use $stderr here
			'psgi.errors'	   => \*STDERR,

			'psgi.multithread'  => Plack::Util::FALSE,
			'psgi.multiprocess' => Plack::Util::TRUE,
			'psgi.run_once'	 => Plack::Util::FALSE,
			'psgi.streaming'	=> Plack::Util::TRUE,
			'psgi.nonblocking'  => Plack::Util::FALSE,

			# harakiri not supported for now
			'psgix.harakiri'	=> Plack::Util::FALSE,
		};

		my $res = Plack::Util::run_app($psgi_app, $env);

		if (ref($res) eq 'ARRAY') {
			$handle_response->($res);
		} elsif (ref($res) eq 'CODE') {
			$res->(sub {
				$handle_response->($_[0]);
			});
		} else {
			die "Bad response $res";
		}

		$req->Finish();
		return 1;
	};
};
)";
