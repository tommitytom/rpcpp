import { describe, expect, it } from 'vitest';

import { frame, FrameParser } from '../src/transport.js';

function bytes(...vals: number[]): Uint8Array {
	return new Uint8Array(vals);
}

function utf8(s: string): Uint8Array {
	return new TextEncoder().encode(s);
}

function decode(bs: Uint8Array): string {
	return new TextDecoder().decode(bs);
}

describe('frame() — line framing', () => {
	it('appends a newline', () => {
		const out = frame(utf8('hello'), 'line');
		expect(decode(out)).toBe('hello\n');
	});
});

describe('frame() — length32 framing', () => {
	it('prepends a 4-byte big-endian length', () => {
		const out = frame(bytes(0xAA, 0xBB, 0xCC), 'length32');
		expect(out).toEqual(bytes(0x00, 0x00, 0x00, 0x03, 0xAA, 0xBB, 0xCC));
	});

	it('handles empty payload', () => {
		const out = frame(new Uint8Array(0), 'length32');
		expect(out).toEqual(bytes(0x00, 0x00, 0x00, 0x00));
	});
});

describe('FrameParser — line', () => {
	it('emits one frame per line', () => {
		const out: string[] = [];
		const p = new FrameParser('line', (f) => out.push(decode(f)));
		p.push(utf8('one\ntwo\nthree\n'));
		expect(out).toEqual(['one', 'two', 'three']);
	});

	it('reassembles split arrivals', () => {
		const out: string[] = [];
		const p = new FrameParser('line', (f) => out.push(decode(f)));
		p.push(utf8('hel'));
		p.push(utf8('lo\nwor'));
		p.push(utf8('ld\n'));
		expect(out).toEqual(['hello', 'world']);
	});
});

describe('FrameParser — length32', () => {
	it('emits frames bounded by length prefix', () => {
		const out: Uint8Array[] = [];
		const p = new FrameParser('length32', (f) => out.push(f));
		const a = frame(utf8('AA'), 'length32');
		const b = frame(utf8('BBBB'), 'length32');
		p.push(concat(a, b));
		expect(out.length).toBe(2);
		expect(decode(out[0]!)).toBe('AA');
		expect(decode(out[1]!)).toBe('BBBB');
	});

	it('reassembles when header and payload arrive in pieces', () => {
		const out: Uint8Array[] = [];
		const p = new FrameParser('length32', (f) => out.push(f));
		const f = frame(utf8('split frame'), 'length32');
		// drip-feed one byte at a time
		for (const b of f) p.push(new Uint8Array([b]));
		expect(out.length).toBe(1);
		expect(decode(out[0]!)).toBe('split frame');
	});

	it('handles back-pressure across many frames', () => {
		const out: Uint8Array[] = [];
		const p = new FrameParser('length32', (f) => out.push(f));
		const all = [
			frame(utf8('a'),    'length32'),
			frame(utf8('bb'),   'length32'),
			frame(utf8('ccc'),  'length32'),
			frame(utf8('dddd'), 'length32'),
		];
		p.push(all.reduce(concat, new Uint8Array(0)));
		expect(out.map(decode)).toEqual(['a', 'bb', 'ccc', 'dddd']);
	});
});

function concat(a: Uint8Array, b: Uint8Array): Uint8Array {
	const out = new Uint8Array(a.length + b.length);
	out.set(a, 0);
	out.set(b, a.length);
	return out;
}
