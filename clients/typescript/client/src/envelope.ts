export type RpcId = string | number | null;

export interface RpcRequest {
	jsonrpc: '2.0';
	id?:     RpcId;
	method:  string;
	params?: unknown[] | Record<string, unknown>;
}

export interface RpcResponse<T = unknown> {
	jsonrpc: '2.0';
	id:      RpcId;
	result:  T;
}

export interface RpcErrorBody {
	code:    number;
	message: string;
	data?:   unknown;
}

export interface RpcErrorEnvelope {
	jsonrpc: '2.0';
	id:      RpcId;
	error:   RpcErrorBody;
}

export interface RpcNotification {
	jsonrpc: '2.0';
	method:  string;
	params?: unknown;
}

export type RpcMessage = RpcResponse | RpcErrorEnvelope | RpcNotification;

export function isResponse(m: unknown): m is RpcResponse {
	return typeof m === 'object' && m !== null
		&& (m as RpcResponse).jsonrpc === '2.0'
		&& 'result' in m
		&& 'id' in m;
}

export function isErrorEnvelope(m: unknown): m is RpcErrorEnvelope {
	return typeof m === 'object' && m !== null
		&& (m as RpcErrorEnvelope).jsonrpc === '2.0'
		&& 'error' in m
		&& 'id' in m;
}

export function isNotification(m: unknown): m is RpcNotification {
	return typeof m === 'object' && m !== null
		&& (m as RpcNotification).jsonrpc === '2.0'
		&& 'method' in m
		&& !('id' in m);
}
