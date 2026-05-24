import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { type OpenRpcDocument, writeService } from '../src/generate-rpc-schema.ts';

function tmp(): { dir: string; cleanup: () => void } {
	const dir = mkdtempSync(join(tmpdir(), 'rpcpp-codegen-'));
	return { dir, cleanup: () => rmSync(dir, { recursive: true, force: true }) };
}

describe('writeService — generated interface', () => {
	it('normalises $ref names so the interface references the camelized types it emitted', async () => {
		// Reproduces the bug where the C++ server uses mangled type names like
		// `trader__engine__wire__Foo` as $defs keys; the codegen emits
		// `export interface TraderEngineWireFoo` (camelized) but the method
		// signatures still referenced the raw mangled name.
		const doc: OpenRpcDocument = {
			methods: [
				{
					name: 'do_thing',
					params: [
						{
							name: 'param0',
							required: true,
							schema: { $ref: '#/components/schemas/trader__engine__wire__Request' },
						},
					],
					result: {
						schema: { $ref: '#/components/schemas/trader__engine__wire__Response' },
					},
				},
			],
			components: {
				schemas: {
					trader__engine__wire__Request:  { type: 'object', properties: { id: { type: 'string' } } },
					trader__engine__wire__Response: { type: 'object', properties: { ok: { type: 'boolean' } } },
				},
			},
		};

		const { dir, cleanup } = tmp();
		try {
			const out = join(dir, 'gen.ts');
			await writeService(doc, 'ts', 'Svc', out);
			const text = readFileSync(out, 'utf8');

			// Both types should be declared with the camelized name.
			expect(text).toMatch(/export interface TraderEngineWireRequest \{/);
			expect(text).toMatch(/export interface TraderEngineWireResponse \{/);

			// And the service interface must reference those camelized names,
			// NOT the raw mangled forms.
			expect(text).toMatch(/do_thing\(param0: TraderEngineWireRequest\): Promise<TraderEngineWireResponse>;/);
			expect(text).not.toMatch(/trader__engine__wire__Request/);
			expect(text).not.toMatch(/trader__engine__wire__Response/);
		} finally {
			cleanup();
		}
	});
});
