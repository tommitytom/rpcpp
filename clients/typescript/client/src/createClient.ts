import type { Codec } from './codec.js';
import {
	isErrorEnvelope,
	isNotification,
	isResponse,
	type RpcId,
	type RpcRequest,
} from './envelope.js';
import { RpcCallError, RpcTransportError } from './errors.js';
import type { Transport } from './transport.js';

export interface ClientOptions {
	transport: Transport;
	codec:     Codec;
}

interface Pending {
	resolve: (value: unknown) => void;
	reject:  (reason: unknown) => void;
}

/**
 * Hidden methods exposed by the proxy alongside generated service methods.
 * Access them with bracket notation: client['$close']().
 */
export interface ClientControl {
	$notify(method: string, ...params: unknown[]): Promise<void>;
	/**
	 * Subscribe to server-pushed JSON-RPC notifications for `method`. The
	 * handler is called with the decoded `params` value for each matching
	 * notification frame the transport delivers. Multiple handlers per
	 * method are supported; each fires independently. Pair with `$off`
	 * (or pass the same handler reference back) to unsubscribe.
	 */
	$on(method: string, handler: (params: unknown) => void): void;
	$off(method: string, handler: (params: unknown) => void): void;
	$close(): Promise<void>;
}

const CLOSE_REASON = Symbol('rpc-client-close');

export function createClient<TService extends object>(
	opts: ClientOptions,
): TService & ClientControl {
	const { transport, codec } = opts;
	const pending = new Map<string | number, Pending>();
	const notificationHandlers = new Map<string, Set<(params: unknown) => void>>();
	let nextId = 1;
	let closed = false;
	let closeError: Error | undefined;

	transport.onFrame((bytes) => {
		let envelope: unknown;
		try {
			envelope = codec.decode(bytes);
		} catch (err) {
			// Malformed frame from server — surface as an error to all pending
			// requests rather than silently dropping it.
			rejectAll(new RpcTransportError('failed to decode frame', { cause: err }));
			return;
		}

		if (isResponse(envelope)) {
			const id = envelope.id;
			if (id === null || id === undefined) return;
			const slot = pending.get(id as string | number);
			if (slot) {
				pending.delete(id as string | number);
				slot.resolve(envelope.result);
			}
			return;
		}

		if (isErrorEnvelope(envelope)) {
			const id = envelope.id;
			if (id === null || id === undefined) return;
			const slot = pending.get(id as string | number);
			if (slot) {
				pending.delete(id as string | number);
				slot.reject(new RpcCallError(
					envelope.error.code,
					envelope.error.message,
					envelope.id,
					envelope.error.data,
				));
			}
			return;
		}

		if (isNotification(envelope)) {
			const set = notificationHandlers.get(envelope.method);
			if (!set) return;
			// Snapshot before iterating so a handler can $off itself without
			// invalidating the loop.
			for (const handler of [...set]) {
				try {
					handler(envelope.params);
				} catch (err) {
					// Don't let a misbehaving handler block its peers or
					// stall the transport — log and continue.
					// eslint-disable-next-line no-console
					console.error(`rpcpp: notification handler for "${envelope.method}" threw`, err);
				}
			}
			return;
		}
		// Unknown envelope shapes are ignored.
	});

	transport.onClose((reason) => {
		closed     = true;
		closeError = reason ?? new RpcTransportError('transport closed');
		rejectAll(closeError);
	});

	function rejectAll(reason: Error): void {
		for (const slot of pending.values()) slot.reject(reason);
		pending.clear();
	}

	async function call(method: string, params: unknown[]): Promise<unknown> {
		if (closed) {
			throw closeError ?? new RpcTransportError('transport closed');
		}
		const id: RpcId = nextId++;
		const request: RpcRequest = {
			jsonrpc: '2.0',
			id,
			method,
			params,
		};
		const promise = new Promise<unknown>((resolve, reject) => {
			pending.set(id as number, { resolve, reject });
		});
		try {
			await transport.send(codec.encode(request));
		} catch (err) {
			pending.delete(id as number);
			throw err;
		}
		return promise;
	}

	async function notify(method: string, params: unknown[]): Promise<void> {
		// Send `id: null` rather than omitting the field. reflect-cpp's
		// msgpack struct reader (used by rpcpp's C++ servers) treats
		// `std::optional<rfl::Generic> id` as required at the parse layer
		// — a missing key produces "Field named 'id' not found." and the
		// whole envelope is dropped, so notifications never reach their
		// handlers. A null id is a no-op semantically: the server still
		// emits a response, but the JS-side onFrame handler discards
		// envelopes whose id is null/undefined.
		const request: RpcRequest = {
			jsonrpc: '2.0',
			id:      null,
			method,
			params,
		};
		await transport.send(codec.encode(request));
	}

	const control: ClientControl = {
		async $notify(method: string, ...params: unknown[]): Promise<void> {
			await notify(method, params);
		},
		$on(method: string, handler: (params: unknown) => void): void {
			let set = notificationHandlers.get(method);
			if (!set) {
				set = new Set();
				notificationHandlers.set(method, set);
			}
			set.add(handler);
		},
		$off(method: string, handler: (params: unknown) => void): void {
			const set = notificationHandlers.get(method);
			if (!set) return;
			set.delete(handler);
			if (set.size === 0) notificationHandlers.delete(method);
		},
		async $close(): Promise<void> {
			rejectAll(new RpcTransportError('client closed'));
			notificationHandlers.clear();
			await transport.close();
		},
	};

	const handler: ProxyHandler<object> = {
		get(_target, prop) {
			if (typeof prop !== 'string') return undefined;
			if (prop === 'then')          return undefined; // not a thenable
			if (prop in control) return (control as unknown as Record<string, unknown>)[prop];
			return (...args: unknown[]) => call(prop, args);
		},
	};

	return new Proxy({}, handler) as TService & ClientControl;
}

export { CLOSE_REASON };
