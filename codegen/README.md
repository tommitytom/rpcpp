# rpcpp-codegen

Generates TypeScript interfaces and [Zod](https://zod.dev) schemas from rpcpp's OpenRPC discovery output. Useful when the C++ server defines the contract and the client code is TypeScript: point this at the schema your server emits and you get a typed client surface plus matching runtime validators.

## Install

```
cd codegen
npm install
```

## Where the schema comes from

Any `TypedRpcServer` instance can dump its OpenRPC document via `server.dumpSchema()` (a JSON string) or expose it over the wire via `addDiscoveryMethod()` (the `rpc.discover` method). For a one-shot grab from the C++ side:

```
echo '{"jsonrpc":"2.0","id":"1","method":"rpc.discover"}' \
  | ./build/examples/example_discovery 2>/dev/null \
  | jq .result > openrpc.json
```

The `jq .result` step strips the JSON-RPC envelope so you're left with the bare OpenRPC document — that's what this tool consumes.

## CLI

```
npm run codegen -- openrpc.json --type=ts  --name=Calculator --out=client.ts
npm run codegen -- openrpc.json --type=zod --name=Calculator --out=client-zod.ts
```

Pipe through stdin instead of passing a path:

```
cat openrpc.json | npm run codegen -- --type=ts --name=Calculator --out=client.ts
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
