# Pox PHP Runtime

This repository builds the independently versioned PHP runtimes loaded by
[Pox](https://github.com/shyim/pox). Each release contains a ZTS/embed PHP
shared library with its native dependencies and a single stable public symbol:
`pox_php_get_api`.

PHP and Zend headers are private implementation details. Pox communicates with
the runtime through the versioned ABI in
[`include/pox_php_runtime.h`](include/pox_php_runtime.h), using opaque handles,
length-delimited buffers, and explicit ownership.

## Local build

With an embed-enabled ZTS PHP installation, pass the native Rust target:

```bash
make test PHP_CONFIG=/path/to/php-config \
  TARGET=x86_64-unknown-linux-gnu
```

On Apple Silicon, for example, use `TARGET=aarch64-apple-darwin`. The resulting
runtime library is `libpox_php.so` on Linux and `libpox_php.dylib` on macOS.

To build PHP and its dependencies through static-php-cli first:

```bash
PHP_VERSION=8.5 \
SPC_LIBC=glibc \
TARGET=x86_64-unknown-linux-gnu \
./scripts/build-php-runtime.sh
```

On macOS, omit `SPC_LIBC` and use `x86_64-apple-darwin` or
`aarch64-apple-darwin` as the target.

The runtime targets PHP 8.4 and 8.5 on Linux glibc/musl and macOS for x86_64
and aarch64. Release archives include runtime metadata and license notices.
The channel index is signed with Ed25519; Pox rejects unsigned or corrupted
downloads. The active verification key is published in
[`keys/runtime-index-ed25519.pub`](keys/runtime-index-ed25519.pub); rotations
add a new trusted key to Pox before changing the release signing secret.

## HTTP response limits (ABI 1.1)

The `POX_FEATURE_RESPONSE_LIMITS` capability enables per-request native body and
serialized-header buffer limits. The existing request layout is unchanged:
`reserved0 & POX_HTTP_RESPONSE_LIMITS` selects `reserved[0]` and `reserved[1]` as
uint32 byte budgets for the body and headers, respectively. Zero is a strict
zero-byte budget. Requests without this flag receive defaults of 32 MiB and
32 KiB. Runtime manifest metadata advertises ABI 1.1; ABI 1.0 table negotiation
remains supported.

Growth never exceeds these limits. Overflow or allocation failure discards the
entire buffered response; further output is consumed without buffer growth until
PHP completes. The response then contains status 502, empty buffers and
`POX_RESPONSE_BUFFER_FAILED` in `reserved0`. This bounds response capture, not
PHP application allocations or PHP output-buffer handlers. Configure PHP memory
and execution controls separately. Streaming and cancellation are not part of
this capability.

`make test` also runs the isolated response-buffer test, including boundary
arithmetic and injected allocation failure. Pox's real-runtime integration suite
covers body/header rejection, subsequent requests, CGI path metadata and Linux
peak-RSS checks for large generated output.

Worker termination (including callback exit and uncaught exceptions) frees any
pending owned request and bootstrap output before releasing that thread's TSRM
resources. The Rust host must replace a terminated worker; requests interrupted
by termination must not be replayed automatically. PHP worker scripts should
leave their loop when `pox_handle_request()` returns false so the host can recycle
or shut them down.

ZTS builds also advertise `POX_FEATURE_PARALLEL_WEB` (bit 1, value 2). The host
creates the web runtime on its owner thread, may execute requests concurrently
on dispatch threads, joins those threads, and destroys the runtime on its owner.
Without a thread attachment, each off-owner web call allocates isolated TSRM
globals, performs full PHP request startup/shutdown, and releases those globals
before returning. Attached threads reuse TSRM globals as described below. Owner-thread calls
retain compatibility with the original sequential API. The capability adds no
ABI fields; hosts must check it before permitting concurrent web calls. INI
parsing uses a per-call cursor, including for persistent worker initialization.

`POX_FEATURE_HTTP_PROTOCOL` (value 4) enables explicit request protocol metadata.
With `POX_HTTP_PROTOCOL` (request flag value 2), `reserved[2]` must contain 1000
for HTTP/1.0 or 1001 for HTTP/1.1. Invalid values are rejected. Without the flag,
the legacy HTTP/1.1 default applies. Both `SERVER_PROTOCOL` and the SAPI internal
protocol reflect this value; the latter is set in the activation hook because
PHP otherwise resets it. For example, PHP's implicit Location status for a POST
is 302 for HTTP/1.0 and 303 for HTTP/1.1. This flag conveys no proxy or TLS trust.

With `ZEND_MAX_EXECUTION_TIMERS`, persistent callbacks reset the configured PHP
`max_execution_time` timer for each request and disable it during the host wait.
Timer expiry follows PHP's fatal bailout path and ends the incarnation; the host
must replace it without replay. This does not implement host-driven cancellation
or guarantee interruption of blocking native extension code. The Linux PHP 8.5.9
build uses Zend's per-thread wall-clock timers; other timer implementations are
not validated by this change.

Both HTTP SAPIs supply a log callback. PHP errors that use the SAPI destination
produce bounded `php_error` JSON records on stderr, including severity and a
`truncated` flag. Each record is shorter than 4096 bytes. Valid UTF-8 is preserved;
control and invalid bytes are escaped. Explicit PHP error-log destinations retain
PHP's normal behavior.

HTTP activation passes the request's owned Authorization value to PHP's native
`php_handle_auth_data` parser. PHP exports its standard authentication variables;
the SAPI additionally sets AUTH_TYPE for parsed Basic/Digest credentials. This is
credential parsing, not credential validation. Raw HTTP_AUTHORIZATION remains
available. SAPI deactivation frees and nulls PHP's parsed authentication fields,
including between persistent callbacks. The host rejects duplicate Authorization
fields before calling PHP.

Hosts must serialize process-wide PHP module lifecycle and INI configuration.
Pox's Rust API holds an exclusive lease for CLI execution, web owners and worker
pools, and rejects conflicting operations with RuntimeBusy. Repeated library
loads reuse cached validated metadata: metadata discovery itself may initialize
embedded PHP and must not run alongside an active mode. This host guard does not
serialize requests within a web owner or worker pool.

Both native HTTP shutdown paths now call `tsrm_shutdown()` after module and SAPI
shutdown, matching PHP embed cleanup. The host must join dispatch/worker threads
before invoking shutdown. Sequential web/worker/CLI transitions are covered by a
real-runtime integration test, including continued execution after rejected
conflicting operations.

`POX_FEATURE_REQUEST_SCHEME` (value 8) permits `POX_HTTP_SECURE` (request flag
value 4). Hosts set it only for HTTPS established by their transport or validated
trusted-proxy metadata. The SAPI exposes REQUEST_SCHEME=http/https and HTTPS=on
only for secure requests, clearing HTTPS on subsequent plain requests. No native
proxy parsing or trust decision occurs in the runtime; the host owns that policy.
The Rust request exposes this as `secure` and rejects secure requests when the
loaded runtime lacks the capability. ABI layouts are unchanged.


`POX_FEATURE_WEB_THREADS` (value 16) enables `web_thread_enter` and
`web_thread_leave`, occupying the first two previously reserved API pointer
slots without changing the table size. Enter attaches isolated TSRM resources to
an off-owner dispatch thread. Repeated web calls still perform full PHP request
startup/shutdown; they reuse thread resources until leave releases them. Enter
rejects the module owner and duplicate attachment. Leave must run on the same
thread after its last synchronous request, before owner shutdown.

The Rust `ParallelWebExecutor::attach` API returns a borrowed, non-Send WebThread
guard. Its lifetime keeps the web owner alive and its destructor detaches on the
calling thread. The HTTP service waits for every thread to attach before opening
the listener and joins all dispatch threads before dropping the module owner.
Existing embedding callers can continue using per-call resource allocation.


### Cooperative request cancellation

`POX_FEATURE_CANCELLATION` (32) enables `cancellation_create`,
`cancellation_request` and `cancellation_release` in three more reserved API
pointer slots. ABI 1.1 table and request sizes are unchanged. A host opts a
request in with `POX_HTTP_CANCELLATION` (8), storing the handle address's low and
high 32-bit words in `request.reserved[3]` and `[4]`. Handles are single-use and
must come from the same loaded runtime. Keep the host reference alive until the
synchronous web call or worker response callback completes.

A native request retains its own reference through cleanup. A mutex protects
binding and detachment of Zend interrupt/timeout flag pointers. Cancellation can
be requested from another host thread; detachment finishes before TSRM teardown
or the next worker callback. Late cancellation therefore cannot target a reused
PHP thread. Pre-cancelled requests do not execute application code. Cancelled web
responses set `POX_RESPONSE_CANCELLED` (2), discard buffered output and let the
host report cancellation. A cancelled worker exits its incarnation without
publishing a successful response. It can then be replaced without replay.

The Rust `PhpRuntime::cancellation()` API returns an Arc-backed
`RequestCancellation`; attach it to `HttpRequest::cancellation` and retain a
clone to call `cancel()`. Rust rejects reusing the same handle. Older runtimes
return `CancellationUnsupported`; requests with `None` use the existing ABI path.

This uses Zend's timeout bailout at an opcode boundary. PHP's error log currently
uses its ordinary maximum-execution-time diagnostic for that bailout, even when
the trigger was a host cancellation. Arbitrary native extension calls and PHP
shutdown callbacks are not guaranteed to terminate promptly. No thread-kill
signal is installed. The HTTP adapter now attaches a control to every PHP job,
requests cancellation on timeout or disconnect, and retries at 50-ms intervals
while expired/cancelled execution remains retained. It keeps admission and
execution permits until native cleanup finishes. Real HTTP tests cover busy
loops, stuck PHP shutdown callbacks, and cancellation recovery in both modes.
A blocking socket read remains retained until its peer releases it; the process
shutdown deadline is the final containment boundary.

Persistent INI configuration now frees at library unload, while continuing to
survive supported web/worker/CLI mode transitions.


### Response output callbacks

`POX_FEATURE_RESPONSE_OUTPUT` (64) enables a synchronous output sink. Set
`POX_HTTP_RESPONSE_OUTPUT` (16) and encode the low/high address words of
`pox_output_callbacks_v1` in request reserved words 5 and 6. Request/API table
sizes remain unchanged. The callback table and userdata must remain valid during
execution; output callbacks finish before the worker completion callback.

The runtime delivers headers once, followed by body chunks no larger than
16 KiB and explicit PHP flush events. It does not allocate a native response-body
buffer on this path. The existing response body limit bounds total delivered
bytes; the header allocation limit still applies. A zero body limit prohibits
output. PHP application allocations and output-buffer handlers remain governed
by PHP's own memory limit.

Callbacks run synchronously on the PHP thread, allowing the host to provide
bounded backpressure. A host sink must unblock on disconnect, cancellation or
its own deadline; native cancellation cannot unwind a Rust callback blocked in
host code. Returning zero rejects output and follows PHP's aborted-connection
path. `ignore_user_abort` can allow PHP to continue, so execution deadlines and
retained permits still matter.

Rust exposes this as `HttpOutput` and the single-use `ResponseOutput` handle in
`HttpRequest::output`. The host receives early headers/output separately from
the final execution result. It must check that result before marking a response
complete. A later fatal, cancellation or output-limit error cannot replace
headers that were already delivered: the eventual transport must abort an
incomplete body. Streamed responses return an empty buffered body. Sink rejection
and unwinding Rust panics produce errors; panic-abort builds still terminate on
a panic, as usual.

The HTTP CLI now uses the sink for body-bearing responses. A 64-KiB prefix
preserves small-response framing; explicit flush or a full prefix starts a
four-chunk queue (16 KiB per chunk). Queue waits observe cancellation, and the
transport enforces stalled-write deadlines. HTTP/1.1 streams use chunked framing,
HTTP/1.0 closes the connection, and HEAD/bodyless responses retain their metadata
rules. An error after commitment aborts the body; only successful PHP completion
allows a normal final chunk. Standard web threads reset exit status before each
request and include shutdown callback exit status in streamed-failure reporting.

Status headers now use PHP's parsed response code. The runtime no longer reads a
fixed offset into the raw HTTP status line, avoiding out-of-bounds access for
short strings such as `header("HTTP/")`.


Build each PHP version in an isolated SPC working directory. SPC's downloaded
source cache can keep a previous PHP archive despite a new `--with-php` argument.
The runtime build recipe explicitly refreshes `php-src` and checks the resulting
SDK version before runtime linking or packaging. If it reports an SDK mismatch,
use a fresh working directory for the requested version; do not relabel the old
SDK. Dependency-download caches may be copied, but the PHP selection must be
refreshed. A new download does not prove an already-extracted source tree changed.


Runtime packaging includes the SDK's collected PHP/extension/library notices from
`$(php-config --prefix)/license`, under `licenses/php` in the archive. Packaging
fails if that directory is missing or empty. For a differently arranged SDK,
set `PHP_LICENSE_DIR` to its collected notices. The packager does not search the
SPC executable directory for source licenses; that is separate from the SDK build
prefix. This copies the SDK's provided notices without changing their contents.
