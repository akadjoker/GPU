# Diagnostics and profiling

Every device maintains an error queue. `pendingErrorCount()` indicates how
many errors can be read; call `getError` until it returns `false`, or call
`clearErrors` when the application has intentionally discarded diagnostics.
`GPUErrorQueue::Capacity` bounds retained errors; an overflow is reported with
`GPUErrorCode::ErrorQueueOverflow`.

Use `gpu::toString` from `GPUDiagnostics.h` to turn error codes, severities, and
operations into stable log labels. `GPUError::message` is a `const char *`; its
ownership and lifetime are not specified in the public header. **REVIEW:**
define that lifetime contract before promising that callers may retain or copy
messages after the error is consumed.

`GPUProfiler` uses device timestamp queries when `timestampQueries` is
available. Surround work with `beginFrame`/`endFrame`, use named scopes, and
collect results when `collectFrame` returns a non-zero count. The implementation
uses `GPUProfiler::FrameLatency` frame slots; exact availability is backend- and
query-dependent. Profiling is optional and must not be treated as a rendering
dependency.
