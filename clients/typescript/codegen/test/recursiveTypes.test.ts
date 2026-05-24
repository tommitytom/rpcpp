import { describe, expect, it } from 'vitest';

import { generate as generateTs } from '../src/json-schema/json-schema-to-ts.ts';
import { generate as generateZod } from '../src/json-schema/json-schema-to-zod.ts';
import type { JSONSchema } from '../src/json-schema/utils.ts';

const recursiveGenericSchema: JSONSchema = {
	$defs: {
		Generic: {
			anyOf: [
				{ type: 'boolean' },
				{ type: 'number' },
				{ type: 'string' },
				{
					type: 'object',
					additionalProperties: { $ref: '#/$defs/Generic' },
				},
				{
					type: 'array',
					items: { $ref: '#/$defs/Generic' },
				},
				{ type: 'null' },
			],
		},
	},
} as unknown as JSONSchema;

describe('json-schema-to-ts', () => {
	it('emits recursive aliases as inline index signatures (TS accepts these but rejects Record<string, T>)', () => {
		const out = generateTs(recursiveGenericSchema);

		// The recursive Generic alias is emitted at all.
		expect(out).toMatch(/export type Generic =/);

		// Object-of-Generic was an inline `{ [key: string]: Generic }`,
		// NOT a `Record<string, Generic>` which would trigger TS2456
		// (circular reference).
		expect(out).toMatch(/\{ \[key: string\]: Generic \}/);
		expect(out).not.toMatch(/Record<string, Generic>/);
	});
});

describe('json-schema-to-zod', () => {
	it('annotates self-referential schemas with z.ZodTypeAny so type inference resolves', () => {
		const out = generateZod(recursiveGenericSchema);

		// The schema const declaration carries the explicit annotation.
		expect(out).toMatch(/export const GenericSchema: z\.ZodTypeAny = /);

		// And uses z.lazy on the recursive branches.
		expect(out).toMatch(/z\.lazy\(\(\) => GenericSchema\)/);
	});

	it('does NOT annotate non-recursive schemas (no false positives)', () => {
		const flat: JSONSchema = {
			$defs: {
				Foo: { type: 'object', properties: { x: { type: 'string' } }, required: ['x'] },
			},
		} as unknown as JSONSchema;
		const out = generateZod(flat);
		expect(out).toMatch(/export const FooSchema = /);
		expect(out).not.toMatch(/export const FooSchema: z\.ZodTypeAny/);
	});
});
