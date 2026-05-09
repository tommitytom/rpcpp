import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import process from 'node:process';

import { writeService, type GenerationType, type OpenRpcDocument } from './generate-rpc-schema.ts';

function usage(): never {
	process.stderr.write(
		'usage: rpcpp-codegen [<openrpc.json>] --type=<ts|zod> --name=<ServiceName> --out=<output>\n' +
		'       (omit <openrpc.json> to read the document from stdin)\n',
	);
	process.exit(2);
}

function parseArgs(argv: string[]) {
	let inputPath: string | undefined;
	let type: string | undefined;
	let name: string | undefined;
	let out: string | undefined;

	for (const a of argv) {
		if (a === '--') continue;
		if (a === '-h' || a === '--help') usage();
		else if (a.startsWith('--type=')) type = a.slice('--type='.length);
		else if (a.startsWith('--name=')) name = a.slice('--name='.length);
		else if (a.startsWith('--out='))  out  = a.slice('--out='.length);
		else if (!inputPath) inputPath = a;
		else usage();
	}

	if (!type || (type !== 'ts' && type !== 'zod')) usage();
	if (!name || !out) usage();

	return {
		inputPath,
		type:    type as GenerationType,
		name:    name!,
		outPath: out!,
	};
}

const { inputPath, type, name, outPath } = parseArgs(process.argv.slice(2));

const json = inputPath
	? readFileSync(inputPath, 'utf-8')
	: readFileSync(0, 'utf-8');

const doc = JSON.parse(json) as OpenRpcDocument;
const absOut = resolve(process.cwd(), outPath);

await writeService(doc, type, name, absOut);
process.stderr.write(`wrote ${absOut}\n`);
