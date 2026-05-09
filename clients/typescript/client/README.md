# @rpcpp/client

TypeScript client for [rpc++](https://github.com/tommitytom/rpc-plus-plus) JSON-RPC 2.0 servers. Pluggable transport (stdio shipped, websocket TBD) and pluggable codec (JSON, msgpack).

## Install

```
pnpm add @rpcpp/client
```

## Quickstart

Generate a typed service interface with [`@rpcpp/codegen`](../codegen), then create a client:

```ts
import { createClient, JsonCodec, spawnStdioTransport } from '@rpcpp/client';
import type { Calculator } from './generated.ts';

const codec     = new JsonCodec();
const transport = spawnStdioTransport(
  { command: './build/examples/example_calculator' },
  codec.framing,
);

const client = createClient<Calculator>({ transport, codec });
console.log(await client.add(2, 3));   // → 5
await client.$close();
```

## Codecs

| Codec         | `framing`    | wire format                                      |
|---------------|--------------|--------------------------------------------------|
| `JsonCodec`   | `'line'`     | UTF-8 JSON, `\n`-delimited                       |
| `MsgpackCodec`| `'length32'` | binary msgpack, 4-byte big-endian length prefix  |

The codec drives both the on-wire encoding *and* the framing strategy, mirroring how `Codec::default_in_framer` is selected on the C++ side.

## Transports

### `StdioTransport`

Construct from any pair of Node streams:

```ts
import { StdioTransport, JsonCodec, createClient } from '@rpcpp/client';

const codec     = new JsonCodec();
const transport = new StdioTransport({
  input:   process.stdin,
  output:  process.stdout,
  framing: codec.framing,
});
```

### `spawnStdioTransport(opts, framing)`

Convenience wrapper around `child_process.spawn`. Returns a `StdioTransport` whose `child` field is the `ChildProcess`, in case you need to wait for exit or kill the server.

## Control methods

The proxy returned by `createClient` exposes two non-RPC helpers:

- `client.$notify(method, ...params)` — fire-and-forget; sends a JSON-RPC notification (no `id`, no response).
- `client.$close()` — closes the underlying transport and rejects any pending calls.

## Errors

- `RpcCallError` — server returned a JSON-RPC error envelope. Has `code`, `message`, `data`, `requestId`.
- `RpcTransportError` — transport- or framing-level failure (write error, decode failure, EOF with pending calls).
