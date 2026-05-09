import type { Codec, Framing } from '../codec.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8');

export class JsonCodec implements Codec {
	readonly isBinary: boolean = false;
	readonly framing:  Framing = 'line';

	encode(value: unknown): Uint8Array {
		return encoder.encode(JSON.stringify(value));
	}

	decode(bytes: Uint8Array): unknown {
		return JSON.parse(decoder.decode(bytes));
	}
}
