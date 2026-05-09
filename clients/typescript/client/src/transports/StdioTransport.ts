import type { Readable, Writable } from 'node:stream';

import type { Framing } from '../codec.js';
import { RpcTransportError } from '../errors.js';
import {
	frame,
	FrameParser,
	type CloseHandler,
	type FrameHandler,
	type Transport,
} from '../transport.js';

export interface StdioTransportOptions {
	input:    Readable;
	output:   Writable;
	framing:  Framing;
}

export class StdioTransport implements Transport {
	private readonly input:    Readable;
	private readonly output:   Writable;
	private readonly framing:  Framing;
	private readonly parser:   FrameParser;
	private readonly frameHandlers: FrameHandler[] = [];
	private readonly closeHandlers: CloseHandler[] = [];
	private closed = false;

	constructor(opts: StdioTransportOptions) {
		this.input   = opts.input;
		this.output  = opts.output;
		this.framing = opts.framing;
		this.parser  = new FrameParser(this.framing, (f) => this.dispatchFrame(f));

		this.input.on('data', (chunk: Buffer | string) => {
			const bytes = typeof chunk === 'string'
				? new TextEncoder().encode(chunk)
				: new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
			this.parser.push(bytes);
		});
		this.input.on('end',   () => this.handleClose());
		this.input.on('close', () => this.handleClose());
		this.input.on('error', (err) => this.handleClose(toError(err)));
		this.output.on('error', (err) => this.handleClose(toError(err)));
	}

	send(payload: Uint8Array): Promise<void> {
		if (this.closed) {
			return Promise.reject(new RpcTransportError('transport is closed'));
		}
		const framed = frame(payload, this.framing);
		return new Promise<void>((resolve, reject) => {
			this.output.write(framed, (err) => {
				if (err) reject(new RpcTransportError('write failed', { cause: err }));
				else     resolve();
			});
		});
	}

	onFrame(handler: FrameHandler): void {
		this.frameHandlers.push(handler);
	}

	onClose(handler: CloseHandler): void {
		this.closeHandlers.push(handler);
	}

	close(): Promise<void> {
		if (this.closed) return Promise.resolve();
		this.closed = true;
		return new Promise<void>((resolve) => {
			this.output.end(() => resolve());
		});
	}

	private dispatchFrame(f: Uint8Array): void {
		for (const h of this.frameHandlers) h(f);
	}

	private handleClose(reason?: Error): void {
		if (this.closed) return;
		this.closed = true;
		for (const h of this.closeHandlers) h(reason);
	}
}

function toError(err: unknown): Error {
	return err instanceof Error ? err : new Error(String(err));
}
