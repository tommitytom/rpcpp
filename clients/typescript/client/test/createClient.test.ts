import { describe, expect, it } from 'vitest';

import { JsonCodec }                       from '../src/codecs/JsonCodec.js';
import { createClient }                    from '../src/createClient.js';
import { RpcCallError, RpcTransportError } from '../src/errors.js';
import type {
	CloseHandler,
	FrameHandler,
	Transport,
} from '../src/transport.js';

interface MockService {
	add(a: number, b: number): Promise<number>;
	echo(s: string):           Promise<string>;
	noop():                    Promise<void>;
}

class MockTransport implements Transport {
	sent: Uint8Array[]             = [];
	private frameHandlers: FrameHandler[] = [];
	private closeHandlers: CloseHandler[] = [];

	send(frame: Uint8Array): Promise<void> {
		this.sent.push(frame);
		return Promise.resolve();
	}
	onFrame(h: FrameHandler): void { this.frameHandlers.push(h); }
	onClose(h: CloseHandler): void { this.closeHandlers.push(h); }
	close(): Promise<void> { return Promise.resolve(); }

	emitFrame(bytes: Uint8Array): void {
		for (const h of this.frameHandlers) h(bytes);
	}
	emitClose(reason?: Error): void {
		for (const h of this.closeHandlers) h(reason);
	}
}

const codec = new JsonCodec();

function lastSent(t: MockTransport): unknown {
	const frame = t.sent.at(-1);
	if (!frame) throw new Error('no frames sent');
	return codec.decode(frame);
}

describe('createClient', () => {
	it('sends a request with a positional params array and resolves with the result', async () => {
		const t      = new MockTransport();
		const client = createClient<MockService>({ transport: t, codec });

		const promise = client.add(2, 3);

		const req = lastSent(t) as { id: number; method: string; params: unknown[] };
		expect(req.method).toBe('add');
		expect(req.params).toEqual([2, 3]);
		expect(typeof req.id).toBe('number');

		t.emitFrame(codec.encode({ jsonrpc: '2.0', id: req.id, result: 5 }));
		await expect(promise).resolves.toBe(5);
	});

	it('allocates monotonically increasing ids', async () => {
		const t      = new MockTransport();
		const client = createClient<MockService>({ transport: t, codec });

		void client.add(1, 1);
		void client.add(2, 2);
		void client.add(3, 3);

		const ids = t.sent.map((f) => (codec.decode(f) as { id: number }).id);
		expect(ids).toEqual([ids[0], ids[0]! + 1, ids[0]! + 2]);
	});

	it('rejects with RpcCallError when the server returns an error envelope', async () => {
		const t      = new MockTransport();
		const client = createClient<MockService>({ transport: t, codec });

		const promise = client.echo('hi');
		const req     = lastSent(t) as { id: number };

		t.emitFrame(codec.encode({
			jsonrpc: '2.0',
			id:      req.id,
			error:   { code: -32601, message: 'method not found' },
		}));

		const err = await promise.catch((e: unknown) => e);
		expect(err).toBeInstanceOf(RpcCallError);
		expect((err as RpcCallError).code).toBe(-32601);
		expect((err as RpcCallError).message).toBe('method not found');
	});

	it('rejects all pending calls when the transport closes', async () => {
		const t      = new MockTransport();
		const client = createClient<MockService>({ transport: t, codec });

		const a = client.add(1, 2);
		const b = client.echo('x');

		t.emitClose(new RpcTransportError('eof'));

		await expect(a).rejects.toBeInstanceOf(RpcTransportError);
		await expect(b).rejects.toBeInstanceOf(RpcTransportError);
	});

	it('rejects new calls after the transport has closed', async () => {
		const t      = new MockTransport();
		const client = createClient<MockService>({ transport: t, codec });

		t.emitClose();
		await expect(client.add(1, 2)).rejects.toBeInstanceOf(RpcTransportError);
	});

	it('$notify sends a request with no id', async () => {
		const t      = new MockTransport();
		const client = createClient<MockService>({ transport: t, codec });

		await client.$notify('ping', 1, 2);
		const req = lastSent(t) as Record<string, unknown>;
		expect(req.method).toBe('ping');
		expect(req.params).toEqual([1, 2]);
		expect('id' in req).toBe(false);
	});
});
