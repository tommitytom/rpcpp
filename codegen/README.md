# rpcpp-codegen

Generates TypeScript interfaces and [Zod](https://zod.dev) schemas from rpcpp's OpenRPC discovery output. Useful when the C++ server defines the contract and the client code is TypeScript: point this at the schema your server emits and you get a typed client surface plus matching runtime validators.

## Install

```
cd codegen
npm install
```

## Where the schema comes from

Any `TypedRpcServer` instance can dump its OpenRPC document via `server.dumpSchema()` (a JSON string) or expose it over the wire via `addDiscoveryMethod()` (the `rpc.discover` method).

## CLI

The most direct path is to point the tool at your server binary; it spawns it, sends `rpc.discover`, parses the response, and writes the output:

```
npm run codegen -- --exec=./build/examples/example_discovery --type=ts --name=Calculator --out=client.ts
```

The server must register `rpc.discover` (`server.addDiscoveryMethod()`) and exit cleanly on stdin EOF — both of which `RpcServer::run()` already does.

If you've already captured the OpenRPC document to a file (e.g., from a long-running server), pass it as a positional argument instead:

```
npm run codegen -- openrpc.json --type=ts  --name=Calculator --out=client.ts
npm run codegen -- openrpc.json --type=zod --name=Calculator --out=client-zod.ts
```

Or pipe through stdin:

```
cat openrpc.json | npm run codegen -- --type=ts --name=Calculator --out=client.ts
```

To hand-roll a discovery call without `--exec` (for example when the server is already running elsewhere), strip the JSON-RPC envelope with `jq`:

```
echo '{"jsonrpc":"2.0","id":"1","method":"rpc.discover"}' \
  | ./your-server 2>/dev/null \
  | jq .result > openrpc.json
```

`--type=ts` emits `interface ServiceName { ... }` plus typed interfaces for every entry under `components.schemas`. `--type=zod` emits matching `z.object(...)` schemas with `z.infer<>` aliases. Output paths are resolved relative to your current directory.

## Programmatic API

```ts
import { writeService, generateZod } from 'rpcpp-codegen';

await writeService(openrpcDoc, 'ts', 'Calculator', '/abs/path/to/client.ts');
// or, just the schema generators:
const zodSrc = generateZod(jsonSchemaWithDefs);
```

`writeService` reads the existing target file (if any) and skips the write when output is unchanged, so it's safe to wire into a watcher.

## Structure

The package is a thin entry point (`src/generate-rpc-schema.ts`) over two JSON-Schema converters under `src/json-schema/`. They each take a schema with `$defs` and emit a string of generated source. `utils.ts` is shared helpers (PascalCase, ref tracking, topological sort, escape) and a small `runCli` harness if you want either converter standalone.

## Requirements

Node 20+ (or anything that can host `tsx`). The package itself is typecheck-only (`noEmit`); runtime invocation goes through `tsx`. If you publish, add a build step and a `bin` field.
