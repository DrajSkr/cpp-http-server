# Benchmark results

## Modified Windows build

| Check | Status | Result |
| --- | --- | --- |
| Windows CMake build | PASS | Built `build/server.exe` using the existing Ninja and Clang configuration. |
| In-process streaming with stalled and partial writes | PASS | Verified 0, 65,536, and 2,097,169 payload bytes, complete framing, and simulated reset handling. Maximum observed pending output was **130,957 bytes** (127.9 KiB) on the simulated slow client. |
| In-process route regression checks | PASS | Root, plain echo, user agent, split binary POST, GET round trip, and gzip decoding. |

## Recorded HTTP benchmark

| Check | Status | Result |
| --- | --- | --- |
| Completed HTTP throughput | PASS | 5,000/5,000 complete HTTP 200 responses at 6,984.67 requests/s with client concurrency set to 100. |
| Simultaneous persistent connections | PASS | 2,000/2,000 TCP connections opened and returned complete HTTP responses after a 0.2 s hold. |
| Chunked streaming | PASS | The HTTP client read the complete 31,148,316-byte response with chunked transfer encoding. |
| Gzip echo | PASS | The decompressed response matched the expected body. |

The recorded HTTP benchmark comes from the existing run captured in [http_validation_results.json](http_validation_results.json).
