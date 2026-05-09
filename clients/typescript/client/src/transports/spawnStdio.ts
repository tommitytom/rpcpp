import { spawn, type ChildProcess, type SpawnOptions } from 'node:child_process';

import type { Framing } from '../codec.js';
import { StdioTransport } from './StdioTransport.js';

export interface SpawnStdioOptions {
	command: string;
	args?:   readonly string[];
	cwd?:    string;
	env?:    NodeJS.ProcessEnv;
	/**
	 * Where the child's stderr should go. Defaults to 'inherit' so that
	 * server logs surface in the parent terminal.
	 */
	stderr?: 'inherit' | 'pipe' | 'ignore';
}

export interface SpawnedStdioTransport extends StdioTransport {
	readonly child: ChildProcess;
}

/**
 * Spawn a child process and return a StdioTransport bound to its stdin/stdout.
 *
 * Mirrors the spawn pattern used by codegen/src/cli.ts:fetchSchemaFromExecutable.
 */
export function spawnStdioTransport(
	opts:    SpawnStdioOptions,
	framing: Framing,
): SpawnedStdioTransport {
	const spawnOpts: SpawnOptions = {
		stdio: ['pipe', 'pipe', opts.stderr ?? 'inherit'],
		cwd:   opts.cwd,
		env:   opts.env,
	};

	const child = spawn(opts.command, opts.args ?? [], spawnOpts);

	if (!child.stdin || !child.stdout) {
		throw new Error(`spawnStdioTransport: failed to obtain stdio streams for ${opts.command}`);
	}

	const transport = new StdioTransport({
		input:   child.stdout,
		output:  child.stdin,
		framing,
	}) as SpawnedStdioTransport;

	(transport as { child: ChildProcess }).child = child;

	return transport;
}
