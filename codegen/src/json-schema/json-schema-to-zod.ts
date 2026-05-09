/* eslint-disable curly */
/**
 * json-schema-to-zod.ts
 *
 * Converts a JSON Schema (draft 2020-12 / draft-07 compatible) into
 * generated Zod TypeScript code.  Supports "barrel" schemas whose
 * root is nothing but a `$defs` block – every definition is emitted
 * as an exported `z` object with an accompanying inferred type.
 *
 * Usage:
 *   npx tsx json-schema-to-zod.ts <input.json> [output.ts]
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
	toPascalCase,
	topoSort,
	typeName,
	type JSONSchema
} from "./utils.ts";

// ── Zod-specific helpers ────────────────────────────────────────────────────

function schemaName(defKey: string): string {
	return `${toPascalCase(defKey)}Schema`;
}

// ── Core conversion ─────────────────────────────────────────────────────────

interface ConvertContext {
	defs: Record<string, JSONSchema>;
	referencedDefs: Set<string>;
}

function convertSchema(schema: JSONSchema, ctx: ConvertContext): string {
	// $ref
	if (schema.$ref) {
		const defKey = refToDefKey(schema.$ref);
		if (defKey) {
			ctx.referencedDefs.add(defKey);
			return `z.lazy(() => ${schemaName(defKey)})`;
		}
		return `z.unknown() /* unresolved $ref: ${schema.$ref} */`;
	}

	// const
	if (schema.const !== undefined) {
		return `z.literal(${JSON.stringify(schema.const)})`;
	}

	// enum
	if (schema.enum) {
		if (schema.enum.length === 1) {
			return `z.literal(${JSON.stringify(schema.enum[0])})`;
		}
		const members = schema.enum.map((v) => JSON.stringify(v)).join(", ");
		if (schema.enum.every((v) => typeof v === "string")) {
			return `z.enum([${members}])`;
		}
		const literals = schema.enum.map((v) => `z.literal(${JSON.stringify(v)})`).join(", ");
		return `z.union([${literals}])`;
	}

	// combinators
	if (schema.allOf) return convertAllOf(schema.allOf, ctx);
	if (schema.oneOf) return convertUnion(schema.oneOf, ctx);
	if (schema.anyOf) return convertUnion(schema.anyOf, ctx);

	// type unions  e.g. type: ["string", "null"]
	if (Array.isArray(schema.type)) {
		const types = schema.type.filter((t) => t !== "null");
		const hasNull = schema.type.includes("null");
		let base: string;
		if (types.length === 1) {
			base = convertSchema({ ...schema, type: types[0] }, ctx);
		} else {
			const members = types.map((t) => convertSchema({ ...schema, type: t }, ctx));
			base = `z.union([${members.join(", ")}])`;
		}
		return hasNull ? `${base}.nullable()` : base;
	}

	// single type
	switch (schema.type) {
		case "string":
			return convertString(schema);
		case "number":
		case "integer":
			return convertNumber(schema);
		case "boolean":
			return "z.boolean()";
		case "null":
			return "z.null()";
		case "object":
			return convertObject(schema, ctx);
		case "array":
			return convertArray(schema, ctx);
	}

	// No explicit type – try to infer
	if (schema.properties || schema.additionalProperties !== undefined) {
		return convertObject({ ...schema, type: "object" }, ctx);
	}
	if (schema.items || schema.prefixItems) {
		return convertArray({ ...schema, type: "array" }, ctx);
	}

	return "z.unknown()";
}

// ── Type-specific converters ────────────────────────────────────────────────

function convertString(schema: JSONSchema): string {
	let s = "z.string()";

	const formatMap: Record<string, string> = {
		email: ".email()",
		uri: ".url()",
		url: ".url()",
		uuid: ".uuid()",
		"date-time": ".datetime()",
		date: ".date()",
		time: ".time()",
		ipv4: '.ip({ version: "v4" })',
		ipv6: '.ip({ version: "v6" })',
	};

	if (schema.format && formatMap[schema.format]) {
		s += formatMap[schema.format];
	}

	if (schema.minLength !== undefined) s += `.min(${schema.minLength})`;
	if (schema.maxLength !== undefined) s += `.max(${schema.maxLength})`;
	if (schema.pattern !== undefined) s += `.regex(new RegExp("${escapeString(schema.pattern)}"))`;

	return applyMeta(s, schema);
}

function convertNumber(schema: JSONSchema): string {
	let s = schema.type === "integer" ? "z.number().int()" : "z.number()";

	if (schema.minimum !== undefined) s += `.gte(${schema.minimum})`;
	if (schema.maximum !== undefined) s += `.lte(${schema.maximum})`;

	if (typeof schema.exclusiveMinimum === "number") {
		s += `.gt(${schema.exclusiveMinimum})`;
	}
	if (typeof schema.exclusiveMaximum === "number") {
		s += `.lt(${schema.exclusiveMaximum})`;
	}
	if (schema.exclusiveMinimum === true && schema.minimum !== undefined) {
		s = s.replace(`.gte(${schema.minimum})`, `.gt(${schema.minimum})`);
	}
	if (schema.exclusiveMaximum === true && schema.maximum !== undefined) {
		s = s.replace(`.lte(${schema.maximum})`, `.lt(${schema.maximum})`);
	}

	if (schema.multipleOf !== undefined) s += `.multipleOf(${schema.multipleOf})`;

	return applyMeta(s, schema);
}

function convertObject(schema: JSONSchema, ctx: ConvertContext): string {
	const required = new Set(schema.required ?? []);
	const props = schema.properties ?? {};
	const propEntries = Object.entries(props);

	if (propEntries.length === 0 && schema.additionalProperties === undefined) {
		return applyMeta("z.object({})", schema);
	}

	const lines: string[] = [];
	for (const [key, propSchema] of propEntries) {
		const identifier = /^[a-zA-Z_$][a-zA-Z0-9_$]*$/.test(key) ? key : `"${escapeString(key)}"`;
		let propCode = convertSchema(propSchema, ctx);
		if (!required.has(key)) {
			propCode += ".optional()";
		}
		lines.push(`${identifier}: ${propCode},`);
	}

	let s: string;
	if (lines.length === 0) {
		s = "z.object({})";
	} else {
		s = `z.object({\n${indent(lines.join("\n"), 1)}\n})`;
	}

	if (schema.additionalProperties === false) {
		s += ".strict()";
	} else if (typeof schema.additionalProperties === "object") {
		s += `.catchall(${convertSchema(schema.additionalProperties, ctx)})`;
	} else if (schema.additionalProperties === true) {
		s += ".passthrough()";
	}

	return applyMeta(s, schema);
}

function convertArray(schema: JSONSchema, ctx: ConvertContext): string {
	const tupleItems = schema.prefixItems ?? (Array.isArray(schema.items) ? schema.items : null);
	if (tupleItems) {
		const members = tupleItems.map((item) => convertSchema(item, ctx)).join(", ");
		const s = `z.tuple([${members}])`;
		return applyArrayConstraints(applyMeta(s, schema), schema);
	}

	let itemsCode = "z.unknown()";
	if (schema.items && !Array.isArray(schema.items)) {
		itemsCode = convertSchema(schema.items, ctx);
	}

	const s = `z.array(${itemsCode})`;
	return applyArrayConstraints(applyMeta(s, schema), schema);
}

function applyArrayConstraints(s: string, schema: JSONSchema): string {
	if (schema.minItems !== undefined) s += `.min(${schema.minItems})`;
	if (schema.maxItems !== undefined) s += `.max(${schema.maxItems})`;
	return s;
}

// ── Combinators ─────────────────────────────────────────────────────────────

function convertAllOf(schemas: JSONSchema[], ctx: ConvertContext): string {
	if (schemas.length === 0) return "z.unknown()";
	if (schemas.length === 1) return convertSchema(schemas[0], ctx);
	const parts = schemas.map((s) => convertSchema(s, ctx));
	return parts.reduce((acc, part) => `${acc}.and(${part})`);
}

function convertUnion(schemas: JSONSchema[], ctx: ConvertContext): string {
	const nullMembers = schemas.filter(
		(s) => s.type === "null" || (s.const === null && Object.keys(s).length <= 2),
	);
	const nonNull = schemas.filter((s) => !nullMembers.includes(s));

	if (nonNull.length === 0) return "z.null()";

	let base: string;
	if (nonNull.length === 1) {
		base = convertSchema(nonNull[0], ctx);
	} else {
		const members = nonNull.map((s) => convertSchema(s, ctx)).join(", ");
		base = `z.union([${members}])`;
	}

	return nullMembers.length > 0 ? `${base}.nullable()` : base;
}

// ── Meta ────────────────────────────────────────────────────────────────────

function applyMeta(code: string, schema: JSONSchema): string {
	let s = code;
	if (schema.nullable) s += ".nullable()";
	if (schema.description) s += `.describe("${escapeString(schema.description)}")`;
	if (schema.default !== undefined) s += `.default(${JSON.stringify(schema.default)})`;
	return s;
}

// ── Top-level generator ─────────────────────────────────────────────────────

export function generate(rootSchema: JSONSchema): string {
	const defs = getDefs(rootSchema);

	const ctx: ConvertContext = {
		defs,
		referencedDefs: new Set(),
	};

	const lines: string[] = [];
	lines.push('import { z } from "zod";');
	lines.push("");

	const defKeys = Object.keys(defs);

	const converted = new Map<string, string>();
	for (const key of defKeys) {
		converted.set(key, convertSchema(defs[key], ctx));
	}

	const sorted = topoSort(defKeys, (key) => collectDirectRefs(defs[key]));

	for (const key of sorted) {
		const code = converted.get(key)!;
		const sName = schemaName(key);
		const tName = typeName(key);

		lines.push(`export const ${sName} = ${code};`);
		lines.push(`export type ${tName} = z.infer<typeof ${sName}>;`);
		lines.push("");
	}

	if (!isBarrelSchema(rootSchema)) {
		const rootCode = convertSchema(rootSchema, ctx);
		lines.push(`export const RootSchema = ${rootCode};`);
		lines.push("export type Root = z.infer<typeof RootSchema>;");
		lines.push("");
	}

	return lines.join("\n");
}

// ── CLI ─────────────────────────────────────────────────────────────────────

//runCli("json-schema-to-zod.ts", generate);
