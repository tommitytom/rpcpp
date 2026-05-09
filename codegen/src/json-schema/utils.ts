/* eslint-disable curly */
/**
 * shared.ts
 *
 * Common types and utilities shared between JSON Schema converters.
 */

import { readFileSync, writeFileSync } from "node:fs";

// ── Lightweight JSON Schema typing ──────────────────────────────────────────

export interface JSONSchema {
	$ref?: string;
	$defs?: Record<string, JSONSchema>;
	definitions?: Record<string, JSONSchema>; // draft-07 compat

	type?: string | string[];
	enum?: unknown[];
	const?: unknown;

	// object
	properties?: Record<string, JSONSchema>;
	required?: string[];
	additionalProperties?: boolean | JSONSchema;
	patternProperties?: Record<string, JSONSchema>;

	// array
	items?: JSONSchema | JSONSchema[];
	prefixItems?: JSONSchema[];
	minItems?: number;
	maxItems?: number;
	uniqueItems?: boolean;

	// string
	minLength?: number;
	maxLength?: number;
	pattern?: string;
	format?: string;

	// number
	minimum?: number;
	maximum?: number;
	exclusiveMinimum?: number | boolean;
	exclusiveMaximum?: number | boolean;
	multipleOf?: number;

	// combinators
	allOf?: JSONSchema[];
	anyOf?: JSONSchema[];
	oneOf?: JSONSchema[];
	not?: JSONSchema;

	// meta
	description?: string;
	default?: unknown;
	title?: string;

	// nullable shorthand (OpenAPI 3.0 style)
	nullable?: boolean;

	[key: string]: unknown;
}

// ── Naming helpers ──────────────────────────────────────────────────────────

/** Turn a `$defs` key like "my-cool-type" into a PascalCase identifier. */
export function toPascalCase(s: string): string {
	return s
		.replace(/[-_\s]+(.)?/g, (_, c: string | undefined) =>
			c ? c.toUpperCase() : "",
		)
		.replace(/^./, (c) => c.toUpperCase());
}

export function typeName(defKey: string): string {
	return toPascalCase(defKey);
}

// ── String / code helpers ───────────────────────────────────────────────────

export function indent(code: string, level: number): string {
	const tabs = "\t".repeat(level);
	return code
		.split("\n")
		.map((line) => (line.trim() === "" ? "" : `${tabs}${line}`))
		.join("\n");
}

export function escapeString(s: string): string {
	return s.replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\n/g, "\\n");
}

// ── Ref resolution ──────────────────────────────────────────────────────────

export function refToDefKey(ref: string): string | null {
	const match = ref.match(/^#\/(\$defs|definitions)\/(.+)$/);
	return match ? match[2] : null;
}

// ── Dependency helpers ──────────────────────────────────────────────────────

export function collectDirectRefs(schema: JSONSchema): string[] {
	const refs: string[] = [];
	const walk = (s: JSONSchema) => {
		if (s.$ref) {
			const key = refToDefKey(s.$ref);
			if (key) refs.push(key);
			return;
		}
		if (s.properties) Object.values(s.properties).forEach(walk);
		if (s.items && !Array.isArray(s.items)) walk(s.items);
		if (Array.isArray(s.items)) s.items.forEach(walk);
		if (s.prefixItems) s.prefixItems.forEach(walk);
		if (s.additionalProperties && typeof s.additionalProperties === "object") {
			walk(s.additionalProperties);
		}
		if (s.allOf) s.allOf.forEach(walk);
		if (s.anyOf) s.anyOf.forEach(walk);
		if (s.oneOf) s.oneOf.forEach(walk);
	};
	walk(schema);
	return [...new Set(refs)];
}

export function topoSort(keys: string[], getDeps: (key: string) => string[]): string[] {
	const visited = new Set<string>();
	const result: string[] = [];
	const visiting = new Set<string>(); // cycle detection

	const visit = (key: string) => {
		if (visited.has(key)) return;
		if (visiting.has(key)) return; // circular – caller handles it
		visiting.add(key);
		for (const dep of getDeps(key)) {
			if (keys.includes(dep)) visit(dep);
		}
		visiting.delete(key);
		visited.add(key);
		result.push(key);
	};

	for (const key of keys) visit(key);
	return result;
}

// ── Barrel detection ────────────────────────────────────────────────────────

/** Returns true if the root schema only contains $defs and no root type. */
export function isBarrelSchema(schema: JSONSchema): boolean {
	return (
		!schema.type &&
		!schema.properties &&
		!schema.allOf &&
		!schema.anyOf &&
		!schema.oneOf &&
		!schema.items &&
		!schema.enum &&
		!schema.const &&
		!schema.$ref
	);
}

/** Merge $defs and definitions (draft-07 compat) into a single record. */
export function getDefs(schema: JSONSchema): Record<string, JSONSchema> {
	return {
		...(schema.$defs ?? {}),
		...(schema.definitions ?? {}),
	};
}

// ── CLI runner ──────────────────────────────────────────────────────────────

export function runCli(
	toolName: string,
	generate: (schema: JSONSchema) => string,
): void {
	const [inputPath, outputPath] = process.argv.slice(2);

	if (!inputPath) {
		console.error(`Usage: npx tsx ${toolName} <input.json> [output.ts]`);
		process.exit(1);
	}

	const raw = readFileSync(inputPath, "utf-8");
	const schema: JSONSchema = JSON.parse(raw);
	const code = generate(schema);

	if (outputPath) {
		writeFileSync(outputPath, code, "utf-8");
		console.error(`✔ Written to ${outputPath}`);
	} else {
		process.stdout.write(code);
	}
}
