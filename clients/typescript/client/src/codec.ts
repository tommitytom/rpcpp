export type Framing = 'line' | 'length32';

export interface Codec {
	readonly isBinary: boolean;
	readonly framing:  Framing;
	encode(value: unknown): Uint8Array;
	decode(bytes: Uint8Array): unknown;
}
