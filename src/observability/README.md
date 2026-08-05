<!-- canonical: observability_domain -- bounded structured process events -->

# Observability domain

`blob_observability` is a standard-library-only structured JSON-lines sink shared by the process,
application, and server boundaries. `StructuredLogger` is a concrete dependency: the composition
root supplies one owned stream and injects the same logger wherever correlated events are needed.
It is neither a singleton nor a hidden global service.

Every line contains a UTC timestamp, severity, and stable event name. Lifecycle state,
request/connection IDs, tick sequence, HTTP status, close code, and typed error context are included
only where meaningful. String fields are ASCII-safe JSON escaped and bounded; truncation is explicit.
One mutex makes each line atomic across the application and server threads. Logging is best-effort
after a failure and never changes the selected process outcome.

Do not log request bodies, headers, raw targets, configuration contents, scenario rows, credentials,
or caller-supplied network identity. Request IDs are validated correlation values, not authority.
