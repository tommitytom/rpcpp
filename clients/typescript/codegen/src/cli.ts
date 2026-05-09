#!/usr/bin/env -S npx tsx
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import process from 'node:process';

import { writeService, type GenerationType, type OpenRpcDocument } from './generate-rpc-schema.ts';

function usage(): never {
	process.stderr.write(
		'usage: rpcpp-codegen [<openrpc.json>] --type=<ts|zod> --name=<ServiceName> --out=<output>\n' +
		'       rpcpp-codegen --exec=<server-binary> --type=<ts|zod> --name=<ServiceName> --out=<output>\n' +
		'       (omit <openrpc.json> and --exec to read the document from stdin)\n',
	);
	process.exit(2);
}

interface ParsedArgs {
	inputPath: string | undefined;
	execPath:  string | undefined;
	type:      GenerationType;
	name:      string;
	outPath:   string;
}

function parseArgs(argv: string[]): ParsedArgs {
	let inputPath: string | undefined;
	let execPath:  string | undefined;
	let type:      string | undefined;
	let name:      string | undefined;
	let out:       string | undefined;

	for (const a of argv) {
		if (a === '--') continue;
		if (a === '-h' || a === '--help') usage();
		else if (a.startsWith('--type=')) type     = a.slice('--type='.length);
		else if (a.startsWith('--name=')) name     = a.slice('--name='.length);
		else if (a.startsWith('--out='))  out      = a.slice('--out='.length);
		else if (a.startsWith('--exec=')) execPath = a.slice('--exec='.length);
		else if (!inputPath) inputPath = a;
		else usage();
	}

	if (!type || (type !== 'ts' && type !== 'zod')) usage();
	if (!name || !out) usage();
	if (inputPath && execPath) {
		process.stderr.write('error: <openrpc.json> and --exec are mutually exclusive\n');
		process.exit(2);
	}

	return {
		inputPath,
		execPath,
		type:    type as GenerationType,
		name:    name!,
		outPath: out!,
	};
}

async function fetchSchemaFromExecutable(execPath: string): Promise<unknown> {
	const child = spawn(execPath, [], { stdio: ['pipe', 'pipe', 'inherit'] });

	let stdout = '';
	child.stdout.setEncoding('utf-8');
	child.stdout.on('data', (chunk: string) => { stdout += chunk; });

	child.stdin.write('{"jsonrpc":"2.0","id":"discover","method":"rpc.discover"}\n');
	child.stdin.end();

	const code = await new Promise<number | null>((res, rej) => {
		child.on('error', rej);
		child.on('close', res);
	});

	if (code !== 0 && code !== null) {
		throw new Error(`${execPath} exited with code ${code}`);
	}

	for (const line of stdout.split(/\r?\n/)) {
		if (!line.trim()) continue;
		let obj: { id?: unknown; result?: unknown; error?: { code: number; message: string } };
		try {
			obj = JSON.parse(line);
		} catch {
			continue; // notification or non-JSON noise
		}
		if (obj.error) {
			throw new Error(`server returned error ${obj.error.code}: ${obj.error.message}`);
		}
		if (obj.result !== undefined && obj.id === 'discover') {
			return obj.result;
		}
	}
	throw new Error(`no rpc.discover response found in output of ${execPath}`);
}

const { inputPath, execPath, type, name, outPath } = parseArgs(process.argv.slice(2));

let doc: OpenRpcDocument;
if (execPath) {
	doc = await fetchSchemaFromExecutable(execPath) as OpenRpcDocument;
} else {
	const json = inputPath
		? readFileSync(inputPath, 'utf-8')
		: readFileSync(0, 'utf-8');
	doc = JSON.parse(json) as OpenRpcDocument;
}

const absOut = resolve(process.cwd(), outPath);
await writeService(doc, type, name, absOut);
process.stderr.write(`wrote ${absOut}\n`);
