import fs from 'fs';
import type { JSONSchema7 } from 'json-schema';
import path from 'path';
import { fileURLToPath } from 'url';
import { generate as generateTs } from './json-schema/json-schema-to-ts.ts';
import { generate as generateZod } from './json-schema/json-schema-to-zod.ts';
import { typeName, type JSONSchema } from './json-schema/utils.ts';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

export interface OpenRpcDocument {
	methods: {
		name: string;
		summary?: string;
		params: Array<{
			name: string;
			required?: boolean;
			schema: JSONSchema7;
		}>;
		result?: {
			schema: JSONSchema7;
		};
	}[];
	components?: {
		schemas?: Record<string, JSONSchema7>;
	};
}


function formatType(name: string, allowNull: boolean = true): string {
	if (name === 'null' && !allowNull) {
		return 'void';
	}

	switch (name) {
		case 'integer':
			return 'number';
		default:
			return name;
	}
}

function replaceInStrings(obj: unknown, search: string | RegExp, replacement: string): unknown {
	if (typeof obj === 'string') {
		return obj.replace(search, replacement);
	}

	if (Array.isArray(obj)) {
		return obj.map((item) => replaceInStrings(item, search, replacement));
	}

	if (obj !== null && typeof obj === 'object') {
		return Object.fromEntries(
			Object.entries(obj).map(([key, value]) => [key, replaceInStrings(value, search, replacement)]),
		);
	}

	return obj;
}

export type GenerationType = 'zod' | 'ts';

export async function writeService(doc: OpenRpcDocument, type: GenerationType, name: string, file: string) {
	let str = '// This file is auto-generated. Do not edit directly.\n\n';

	const allSchemas: JSONSchema7 = {
		$schema: 'http://json-schema.org/draft-07/schema#',
		$defs: {},
	};

	if (doc.components?.schemas) {
		for (const [schemaName, schema] of Object.entries(doc.components.schemas)) {
			const cloned = replaceInStrings(
				structuredClone(schema),
				'#/components/schemas/',
				'#/$defs/',
			) as unknown as JSONSchema7;
			allSchemas.$defs![schemaName] = cloned;
		}

		if (type === 'ts') {
			str += generateTs(allSchemas as JSONSchema) + '\n';
		} else {
			str += generateZod(allSchemas as JSONSchema) + '\n';
		}
	}

	str += `export interface ${name} {\n`;

	str +=
		doc.methods
			.map((method) => {
				let methodStr = `\t${method.name}(`;

				methodStr +=
					method.params
						.map((p) => {
							if (p.schema?.type) {
								return `${p.name}: ${formatType(p.schema.type as string)}`;
							}

							if (p.schema?.$ref) {
								// The $defs key is whatever the server emitted
								// (often a C++ mangled name like
								// `trader__engine__wire__Foo`); the type emitter
								// normalises it through typeName() before
								// declaring `export type Foo = ...`. We must do
								// the same here or the interface references
								// unbound symbols.
								const refKey = p.schema.$ref.split('/').pop();
								return `${p.name}: ${refKey ? typeName(refKey) : 'any'}`;
							}

							return `${p.name}: any`;
						})
						.join(', ') + '): ';

				const resultSchema = method.result?.schema;
				let returnType: string;
				if (resultSchema?.type) {
					returnType = formatType(resultSchema.type as string, false);
				} else if (resultSchema?.$ref) {
					const refKey = resultSchema.$ref.split('/').pop();
					returnType = refKey ? typeName(refKey) : 'void';
				} else {
					returnType = 'void';
				}
				methodStr += `Promise<${returnType}>;`;

				return methodStr;
			})
			.join('\n') + '\n}\n';

	//const formatted = await format(str, { parser: "typescript" });

	const filePath = path.isAbsolute(file) ? file : path.join(__dirname, file);
	let currentContent = '';
	try { currentContent = fs.readFileSync(filePath, 'utf-8'); } catch { /* not yet written */ }

	if (currentContent !== str) {
		fs.writeFileSync(filePath, str);
	}
}
