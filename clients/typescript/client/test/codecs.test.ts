import { describe, expect, it } from 'vitest';

import { JsonCodec }    from '../src/codecs/JsonCodec.js';
import { MsgpackCodec } from '../src/codecs/MsgpackCodec.js';
import type { RpcRequest, RpcResponse } from '../src/envelope.js';

const sampleRequest: RpcRequest = {
	jsonrpc: '2.0',
	id:      42,
	method:  'add',
	params:  [2, 3],
};

const sampleResponse: RpcResponse<{ x: number; y: number }> = {
	jsonrpc: '2.0',
	id:      'r-1',
	result:  { x: 4, y: 6 },
};

describe('JsonCodec', () => {
	it('reports JSON+line config', () => {
		const c = new JsonCodec();
		expect(c.isBinary).toBe(false);
		expect(c.framing).toBe('line');
	});

	it('round-trips an RpcRequest', () => {
		const c       = new JsonCodec();
		const decoded = c.decode(c.encode(sampleRequest));
		expect(decoded).toEqual(sampleRequest);
	});

	it('round-trips an RpcResponse', () => {
		const c       = new JsonCodec();
		const decoded = c.decode(c.encode(sampleResponse));
		expect(decoded).toEqual(sampleResponse);
	});
});

describe('MsgpackCodec', () => {
	it('reports msgpack+length32 config', () => {
		const c = new MsgpackCodec();
		expect(c.isBinary).toBe(true);
		expect(c.framing).toBe('length32');
	});

	it('round-trips an RpcRequest', () => {
		const c       = new MsgpackCodec();
		const decoded = c.decode(c.encode(sampleRequest));
		expect(decoded).toEqual(sampleRequest);
	});

	it('round-trips an RpcResponse', () => {
		const c       = new MsgpackCodec();
		const decoded = c.decode(c.encode(sampleResponse));
		expect(decoded).toEqual(sampleResponse);
	});
});
