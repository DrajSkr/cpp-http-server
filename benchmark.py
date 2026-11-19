#!/usr/bin/env python3
"""HTTP server claim validation. Python 3.10+, pip install aiohttp.

Measures completed successful HTTP responses rather than submissions. Also checks
simultaneously held TCP connections, streaming, HTTP chunked transfer encoding,
and gzip. Intended for local development, not internet-facing stress tests.
"""
import argparse
import asyncio
import gzip
import json
import time
from collections import Counter
from urllib.parse import urlsplit

import aiohttp


async def throughput(base, total, concurrency, timeout):
    started = time.perf_counter()
    statuses = Counter()
    errors = Counter()
    timings = []
    completed_bytes = 0
    sem = asyncio.Semaphore(concurrency)
    connector = aiohttp.TCPConnector(limit=concurrency, limit_per_host=concurrency)
    client_timeout = aiohttp.ClientTimeout(total=timeout)
    async with aiohttp.ClientSession(connector=connector, timeout=client_timeout) as session:
        async def one_request():
            nonlocal completed_bytes
            async with sem:
                t0 = time.perf_counter()
                try:
                    async with session.get(f'{base}/') as resp:
                        data = await resp.read()  # wait for entire response, not headers
                        statuses[resp.status] += 1
                        if resp.status == 200:
                            completed_bytes += len(data)
                            timings.append(time.perf_counter() - t0)
                except Exception as e:
                    errors[type(e).__name__] += 1
        await asyncio.gather(*(one_request() for _ in range(total)))
    elapsed = time.perf_counter() - started
    successful = statuses[200]
    sorted_t = sorted(timings)
    def percentile(p):
        if not sorted_t:
            return None
        return round(sorted_t[min(len(sorted_t)-1, int((len(sorted_t)-1)*p))]*1000, 2)
    return dict(attempted=total, ok_200=successful, other_statuses=dict(statuses),
                errors=dict(errors), elapsed_sec=round(elapsed, 3),
                successful_req_per_sec=round(successful/elapsed, 2),
                complete_body_bytes=completed_bytes,
                latency_ms_p50=percentile(.50), latency_ms_p95=percentile(.95),
                latency_ms_p99=percentile(.99),
                pass_2200_req_s=(successful == total and successful/elapsed >= 2200))


async def simultaneous_connections(base, target, timeout, hold_sec):
    """Create persistent sockets, require ALL to be simultaneously alive, then
    verify that each can serve a complete HTTP response while still connected.
    This is different from 2,000 requests performed in four batches of 500.
    """
    url = urlsplit(base)
    host = url.hostname or '127.0.0.1'
    port = url.port or 80
    if url.scheme != 'http':
        raise ValueError('Only plain HTTP supported for raw socket test')
    sockets = []
    open_errors = Counter()
    sem = asyncio.Semaphore(200)

    async def open_one():
        async with sem:
            try:
                reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), timeout=timeout)
                sockets.append((reader, writer))
            except Exception as exc:
                open_errors[type(exc).__name__] += 1

    await asyncio.gather(*(open_one() for _ in range(target)))
    opened = len(sockets)
    # Keep the hold duration well below server's configured five-second idle timeout.
    await asyncio.sleep(hold_sec)
    responses_ok = 0
    failures = Counter()

    async def validate_one(reader, writer):
        nonlocal responses_ok
        try:
            writer.write(f'GET / HTTP/1.1\r\nHost: {host}\r\nConnection: keep-alive\r\n\r\n'.encode())
            await asyncio.wait_for(writer.drain(), timeout=timeout)
            header = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), timeout=timeout)
            status = header.split(b'\r\n', 1)[0]
            lengths = [line for line in header.split(b'\r\n') if line.lower().startswith(b'content-length:')]
            if not status.startswith(b'HTTP/1.1 200') or not lengths:
                failures['invalid_status_or_headers'] += 1
                return
            length = int(lengths[0].split(b':', 1)[1].strip())
            await asyncio.wait_for(reader.readexactly(length), timeout=timeout)
            responses_ok += 1
        except Exception as exc:
            failures[type(exc).__name__] += 1

    try:
        await asyncio.gather(*(validate_one(r,w) for r,w in sockets))
    finally:
        for _, writer in sockets:
            writer.close()
        await asyncio.gather(*(writer.wait_closed() for _,writer in sockets), return_exceptions=True)

    return dict(target=target, tcp_connections_opened=opened,
                validated_complete_http_responses=responses_ok,
                open_errors=dict(open_errors), response_errors=dict(failures),
                hold_before_verification_sec=hold_sec,
                pass_2000_simultaneous=(target >= 2000 and opened == target and responses_ok == target),
                caveat='Server has a 5-second idle timeout; run locally and keep hold_sec short.')


async def features(base, timeout, stream_max_bytes):
    result = {}
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=timeout),
                                     auto_decompress=False) as session:
        async with session.get(f'{base}/echo/hello', headers={'Accept-Encoding':'gzip'}) as resp:
            raw = await resp.read()
            result['gzip_echo'] = dict(status=resp.status,
                content_encoding=resp.headers.get('Content-Encoding'),
                decompressed_matches=(gzip.decompress(raw) == b'hello' if resp.headers.get('Content-Encoding') == 'gzip' else False))
        try:
            async with session.get(f'{base}/stream') as resp:
                transfer = resp.headers.get('Transfer-Encoding', '')
                length = resp.headers.get('Content-Length')
                received = 0
                truncated = False
                async for piece in resp.content.iter_chunked(65536):
                    received += len(piece)
                    if received > stream_max_bytes:
                        truncated = True
                        break
                result['stream'] = dict(status=resp.status,
                    transfer_encoding=transfer or None, content_length=length,
                    body_bytes_read=received, interrupted_due_to_test_cap=truncated,
                    is_http_chunked=('chunked' in transfer.lower()),
                    complete_body=(not truncated and ('chunked' in transfer.lower() or
                        (length is not None and received == int(length)))))
        except Exception as exc:
            result['stream'] = dict(error=f'{type(exc).__name__}: {exc}')
    return result


async def main():
    p = argparse.ArgumentParser(description='Validate HTTP server resume claims')
    p.add_argument('--url', default='http://127.0.0.1:4221')
    p.add_argument('--requests', type=int, default=5000)
    p.add_argument('--concurrency', type=int, default=100)
    p.add_argument('--connections', type=int, default=2000)
    p.add_argument('--timeout', type=float, default=15)
    p.add_argument('--hold', type=float, default=0.2)
    p.add_argument('--max-stream-mb', type=int, default=64)
    p.add_argument('--skip-connections', action='store_true')
    p.add_argument('--skip-stream', action='store_true')
    args = p.parse_args()
    base = args.url.rstrip('/')
    output = {}
    print('Testing completed HTTP throughput...', flush=True)
    output['throughput'] = await throughput(base, args.requests, args.concurrency, args.timeout)
    print(json.dumps(output['throughput'], indent=2), flush=True)
    if not args.skip_connections:
        print('Testing simultaneous persistent connections...', flush=True)
        output['simultaneous_connections'] = await simultaneous_connections(base, args.connections, args.timeout, args.hold)
        print(json.dumps(output['simultaneous_connections'], indent=2), flush=True)
    if not args.skip_stream:
        print('Testing actual response features...', flush=True)
        output['features'] = await features(base, args.timeout, args.max_stream_mb*1024*1024)
        print(json.dumps(output['features'], indent=2), flush=True)
    with open('http_validation_results.json', 'w', encoding='utf-8') as f:
        json.dump(output, f, indent=2)
    print('Saved: http_validation_results.json')

if __name__ == '__main__':
    asyncio.run(main())
