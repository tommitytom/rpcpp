import type { RpcId } from './envelope.js';

export class RpcCallError extends Error {
	readonly code:      number;
	readonly data:      unknown;
	readonly requestId: RpcId;

	constructor(code: number, message: string, requestId: RpcId, data?: unknown) {
		super(message);
		this.name      = 'RpcCallError';
		this.code      = code;
		this.data      = data;
		this.requestId = requestId;
	}
}

export class RpcTransportError extends Error {
	constructor(message: string, options?: { cause?: unknown }) {
		super(message, options);
		this.name = 'RpcTransportError';
	}
}
