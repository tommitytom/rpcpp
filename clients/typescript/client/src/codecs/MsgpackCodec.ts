import { encode, decode } from '@msgpack/msgpack';

import type { Codec, Framing } from '../codec.js';

export class MsgpackCodec implements Codec {
	readonly isBinary: boolean = true;
	readonly framing:  Framing = 'length32';

	encode(value: unknown): Uint8Array {
		return encode(value);
	}

	decode(bytes: Uint8Array): unknown {
		return decode(bytes);
	}
}
