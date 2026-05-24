/* eslint-disable curly */
/**
 * json-schema-to-ts.ts
 *
 * Converts a JSON Schema (draft 2020-12 / draft-07 compatible) into
 * TypeScript interfaces and type aliases.  Supports "barrel" schemas
 * whose root is nothing but a `$defs` block – every definition is
 * emitted as an exported interface or type.
 *
 * Usage:
 *   npx tsx json-schema-to-ts.ts <input.json> [output.ts]
 *
 * If no output path is given the generated code is written to stdout.
 */

import {
	collectDirectRefs,
	escapeString,
	getDefs,
	indent,
	isBarrelSchema,
	refToDefKey,
	topoSort,
	typeName,
	type JSONSchema
} from "./utils.ts";

// ── Core conversion ─────────────────────────────────────────────────────────

interface ConvertContext {
	defs: Record<string, JSONSchema>;
}

/**
 * Convert a JSON Schema node into a TypeScript type expression (inline).
 * For top-level named types we use `emitNamedType` instead.
 */
function convertSchema(schema: JSONSchema, ctx: ConvertContext): string {
	// $ref
	if (schema.$ref) {
		const defKey = refToDefKey(schema.$ref);
		if (defKey) return typeName(defKey);
		return `unknown /* unresolved $ref: ${schema.$ref} */`;
	}

	// const
	if (schema.const !== undefined) {
		return literalType(schema.const);
	}

	// enum
	if (schema.enum) {
		return schema.enum.map((v) => literalType(v)).join(" | ");
	}

	// combinators
	if (schema.allOf) return convertAllOf(schema.allOf, ctx);
	if (schema.oneOf) return convertUnion(schema.oneOf, ctx);
	if (schema.anyOf) return convertUnion(schema.anyOf, ctx);

	// type: ["string", "null"]
	if (Array.isArray(schema.type)) {
		const types = schema.type.filter((t) => t !== "null");
		const hasNull = schema.type.includes("null");
		let base: string;
		if (types.length === 1) {
			base = convertSchema({ ...schema, type: types[0] }, ctx);
		} else if (types.length === 0) {
			base = "never";
		} else {
			base = types.map((t) => convertSchema({ ...schema, type: t }, ctx)).join(" | ");
		}
		return wrapNullable(base, hasNull || !!schema.nullable);
	}

	// single type
	switch (schema.type) {
		case "string":
			return wrapNullable(convertString(schema), !!schema.nullable);
		case "number":
		case "integer":
			return wrapNullable("number", !!schema.nullable);
		case "boolean":
			return wrapNullable("boolean", !!schema.nullable);
		case "null":
			return "null";
		case "object":
			return wrapNullable(convertObject(schema, ctx), !!schema.nullable);
		case "array":
			return wrapNullable(convertArray(schema, ctx), !!schema.nullable);
	}

	// No explicit type – try to infer
	if (schema.properties || schema.additionalProperties !== undefined) {
		return wrapNullable(convertObject({ ...schema, type: "object" }, ctx), !!schema.nullable);
	}
	if (schema.items || schema.prefixItems) {
		return wrapNullable(convertArray({ ...schema, type: "array" }, ctx), !!schema.nullable);
	}

	return "unknown";
}

// ── Helpers ─────────────────────────────────────────────────────────────────

function literalType(value: unknown): string {
	if (value === null) return "null";
	if (typeof value === "string") return `"${escapeString(value)}"`;
	if (typeof value === "number" || typeof value === "boolean") return String(value);
	return `${JSON.stringify(value)}`;
}

function wrapNullable(type: string, nullable: boolean): string {
	if (!nullable) return type;
	// Avoid double-wrapping
	if (type === "null" || type.endsWith(" | null")) return type;
	// Wrap unions in parens for clarity
	if (type.includes(" | ") || type.includes(" & ")) return `(${type}) | null`;
	return `${type} | null`;
}

function needsQuoting(key: string): boolean {
	return !/^[a-zA-Z_$][a-zA-Z0-9_$]*$/.test(key);
}

function propKey(key: string): string {
	return needsQuoting(key) ? `"${escapeString(key)}"` : key;
}

// ── String type ─────────────────────────────────────────────────────────────

function convertString(schema: JSONSchema): string {
	// Some formats have well-known TS types; most are just `string`.
	// We emit a JSDoc @format comment for documentation but the TS
	// type remains `string`.
	return "string";
}

// ── Object type ─────────────────────────────────────────────────────────────

function convertObject(schema: JSONSchema, ctx: ConvertContext): string {
	const required = new Set(schema.required ?? []);
	const props = schema.properties ?? {};
	const propEntries = Object.entries(props);

	// Index-signature shorthand for additionalProperties-only objects.
	// We use the inline `{ [key: string]: V }` form rather than
	// `Record<string, V>` so recursive aliases like
	//   export type Generic = boolean | string | { [k: string]: Generic } | Generic[];
	// type-check. `Record<string, T>` would trip TS2456 (circular reference)
	// because it's a plain type alias, while the inline index signature is a
	// "real" object type with structural recursion.
	if (propEntries.length === 0) {
		if (typeof schema.additionalProperties === "object") {
			const valueType = convertSchema(schema.additionalProperties, ctx);
			return `{ [key: string]: ${valueType} }`;
		}
		if (schema.additionalProperties === true || schema.additionalProperties === undefined) {
			// Bare object with no props and no restriction
			return "{ [key: string]: unknown }";
		}
		// additionalProperties: false, no properties → empty object
		return "Record<string, never>";
	}

	const lines: string[] = [];

	for (const [key, propSchema] of propEntries) {
		// JSDoc comment for description
		if (propSchema.description) {
			lines.push(`/** ${propSchema.description} */`);
		}

		const optional = required.has(key) ? "" : "?";
		const readonly_ = (propSchema as Record<string, unknown>).readOnly ? "readonly " : "";
		const tsType = convertSchema(propSchema, ctx);
		lines.push(`${readonly_}${propKey(key)}${optional}: ${tsType};`);
	}

	// Index signature for additionalProperties
	if (typeof schema.additionalProperties === "object") {
		const valueType = convertSchema(schema.additionalProperties, ctx);
		lines.push(`[key: string]: ${valueType};`);
	} else if (schema.additionalProperties === true) {
		lines.push("[key: string]: unknown;");
	}

	return `{\n${indent(lines.join("\n"), 1)}\n}`;
}

// ── Array type ──────────────────────────────────────────────────────────────

function convertArray(schema: JSONSchema, ctx: ConvertContext): string {
	// Tuple
	const tupleItems = schema.prefixItems ?? (Array.isArray(schema.items) ? schema.items : null);
	if (tupleItems) {
		const members = tupleItems.map((item) => convertSchema(item, ctx)).join(", ");
		return `[${members}]`;
	}

	if (schema.items && !Array.isArray(schema.items)) {
		const itemType = convertSchema(schema.items, ctx);
		// Wrap complex union/intersection item types in parens for readability
		if (itemType.includes(" | ") || itemType.includes(" & ")) {
			return `(${itemType})[]`;
		}
		return `${itemType}[]`;
	}

	return "unknown[]";
}

// ── Combinators ─────────────────────────────────────────────────────────────

function convertAllOf(schemas: JSONSchema[], ctx: ConvertContext): string {
	if (schemas.length === 0) return "unknown";
	if (schemas.length === 1) return convertSchema(schemas[0], ctx);
	return schemas.map((s) => convertSchema(s, ctx)).join(" & ");
}

function convertUnion(schemas: JSONSchema[], ctx: ConvertContext): string {
	const nullMembers = schemas.filter(
		(s) => s.type === "null" || (s.const === null && Object.keys(s).length <= 2),
	);
	const nonNull = schemas.filter((s) => !nullMembers.includes(s));

	if (nonNull.length === 0) return "null";

	const base =
		nonNull.length === 1
			? convertSchema(nonNull[0], ctx)
			: nonNull.map((s) => convertSchema(s, ctx)).join(" | ");

	return nullMembers.length > 0 ? wrapNullable(base, true) : base;
}

// ── Named type emission ─────────────────────────────────────────────────────

/**
 * Decide whether a $def should be emitted as an `interface` (for plain
 * object types with properties) or a `type` alias (for everything else).
 */
function emitNamedType(key: string, schema: JSONSchema, ctx: ConvertContext): string {
	const name = typeName(key);
	const lines: string[] = [];

	// JSDoc block
	const docParts: string[] = [];
	if (schema.description) docParts.push(schema.description);
	if (schema.title && schema.title !== key) docParts.push(`@title ${schema.title}`);
	if (docParts.length > 0) {
		if (docParts.length === 1 && !docParts[0].includes("\n")) {
			lines.push(`/** ${docParts[0]} */`);
		} else {
			lines.push("/**");
			for (const part of docParts) {
				for (const line of part.split("\n")) {
					lines.push(` * ${line}`);
				}
			}
			lines.push(" */");
		}
	}

	// Use interface for plain object types, type alias for everything else
	const isPlainObject =
		(schema.type === "object" || (!schema.type && schema.properties)) &&
		!schema.allOf &&
		!schema.anyOf &&
		!schema.oneOf &&
		!schema.nullable &&
		!schema.$ref;

	if (isPlainObject) {
		lines.push(...emitInterface(name, schema, ctx));
	} else {
		const tsType = convertSchema(schema, ctx);
		lines.push(`export type ${name} = ${tsType};`);
	}

	return lines.join("\n");
}

function emitInterface(name: string, schema: JSONSchema, ctx: ConvertContext): string[] {
	const required = new Set(schema.required ?? []);
	const props = schema.properties ?? {};
	const propEntries = Object.entries(props);
	const lines: string[] = [];

	// Determine if we need an extends clause (additionalProperties as index sig
	// goes inside the body, but we can use `extends` for allOf object merges
	// if needed in the future).

	lines.push(`export interface ${name} {`);

	for (const [key, propSchema] of propEntries) {
		if (propSchema.description) {
			lines.push(indent(`/** ${propSchema.description} */`, 1));
		}
		const optional = required.has(key) ? "" : "?";
		const readonly_ = (propSchema as Record<string, unknown>).readOnly ? "readonly " : "";
		const tsType = convertSchema(propSchema, ctx);
		lines.push(indent(`${readonly_}${propKey(key)}${optional}: ${tsType};`, 1));
	}

	// Index signature
	if (typeof schema.additionalProperties === "object") {
		const valueType = convertSchema(schema.additionalProperties, ctx);
		lines.push(indent(`[key: string]: ${valueType};`, 1));
	} else if (schema.additionalProperties === true) {
		lines.push(indent("[key: string]: unknown;", 1));
	}

	lines.push("}");
	return lines;
}

// ── Top-level generator ─────────────────────────────────────────────────────

export function generate(rootSchema: JSONSchema): string {
	const defs = getDefs(rootSchema);
	const ctx: ConvertContext = { defs };

	const output: string[] = [];

	const defKeys = Object.keys(defs);
	const sorted = topoSort(defKeys, (key) => collectDirectRefs(defs[key]));

	for (const key of sorted) {
		output.push(emitNamedType(key, defs[key], ctx));
		output.push("");
	}

	// Root schema (if not barrel-only)
	if (!isBarrelSchema(rootSchema)) {
		const rootType = convertSchema(rootSchema, ctx);
		output.push(`export type Root = ${rootType};`);
		output.push("");
	}

	return output.join("\n");
}

// ── CLI ─────────────────────────────────────────────────────────────────────

//runCli("json-schema-to-ts.ts", generate);
