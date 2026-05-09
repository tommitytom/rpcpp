export { createClient, type ClientOptions, type ClientControl } from './createClient.js';

export type { Codec, Framing } from './codec.js';
export { JsonCodec }    from './codecs/JsonCodec.js';
export { MsgpackCodec } from './codecs/MsgpackCodec.js';

export {
	frame,
	FrameParser,
	type Transport,
	type FrameHandler,
	type CloseHandler,
} from './transport.js';

export {
	StdioTransport,
	type StdioTransportOptions,
} from './transports/StdioTransport.js';

export {
	spawnStdioTransport,
	type SpawnStdioOptions,
	type SpawnedStdioTransport,
} from './transports/spawnStdio.js';

export {
	isErrorEnvelope,
	isNotification,
	isResponse,
	type RpcErrorBody,
	type RpcErrorEnvelope,
	type RpcId,
	type RpcMessage,
	type RpcNotification,
	type RpcRequest,
	type RpcResponse,
} from './envelope.js';

export { RpcCallError, RpcTransportError } from './errors.js';
