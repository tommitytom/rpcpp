import type { Framing } from './codec.js';

export type FrameHandler = (frame: Uint8Array) => void;
export type CloseHandler = (reason?: Error) => void;

export interface Transport {
	send(frame: Uint8Array): Promise<void>;
	onFrame(handler: FrameHandler): void;
	onClose(handler: CloseHandler): void;
	close(): Promise<void>;
}

/**
 * Reassembles raw bytes into framed payloads.
 *
 * Mirrors src/Framer.h:
 *   - 'line':     payloads delimited by '\n'
 *   - 'length32': payloads prefixed with a 4-byte big-endian length
 */
export class FrameParser {
	private buffer: Uint8Array = new Uint8Array(0);
	private readonly emit: FrameHandler;
	private readonly framing: Framing;

	constructor(framing: Framing, emit: FrameHandler) {
		this.framing = framing;
		this.emit    = emit;
	}

	push(chunk: Uint8Array): void {
		this.buffer = concat(this.buffer, chunk);
		if (this.framing === 'line') this.drainLine();
		else                          this.drainLength32();
	}

	private drainLine(): void {
		let start = 0;
		for (let i = 0; i < this.buffer.length; i++) {
			if (this.buffer[i] === 0x0A /* \n */) {
				this.emit(this.buffer.slice(start, i));
				start = i + 1;
			}
		}
		if (start > 0) {
			this.buffer = this.buffer.slice(start);
		}
	}

	private drainLength32(): void {
		while (this.buffer.length >= 4) {
			const len =
				(this.buffer[0]! << 24) |
				(this.buffer[1]! << 16) |
				(this.buffer[2]! << 8)  |
				 this.buffer[3]!;
			const total = 4 + (len >>> 0);
			if (this.buffer.length < total) return;
			this.emit(this.buffer.slice(4, total));
			this.buffer = this.buffer.slice(total);
		}
	}
}

/** Wrap a payload in framing bytes for transmission. */
export function frame(payload: Uint8Array, framing: Framing): Uint8Array {
	if (framing === 'line') {
		const out = new Uint8Array(payload.length + 1);
		out.set(payload, 0);
		out[payload.length] = 0x0A;
		return out;
	}
	const out = new Uint8Array(4 + payload.length);
	const len = payload.length >>> 0;
	out[0] = (len >>> 24) & 0xFF;
	out[1] = (len >>> 16) & 0xFF;
	out[2] = (len >>> 8)  & 0xFF;
	out[3] =  len         & 0xFF;
	out.set(payload, 4);
	return out;
}

function concat(a: Uint8Array, b: Uint8Array): Uint8Array {
	if (a.length === 0) return b;
	if (b.length === 0) return a;
	const out = new Uint8Array(a.length + b.length);
	out.set(a, 0);
	out.set(b, a.length);
	return out;
}
