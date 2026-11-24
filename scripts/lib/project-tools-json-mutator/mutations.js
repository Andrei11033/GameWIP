'use strict';

const { parseJsonSource } = require('./source_parser');

// ------------------------------------------------------------
// Resolve mutation paths and verify source values
// ------------------------------------------------------------

function scalarValue(node) {
    if (!['string', 'number', 'boolean', 'null'].includes(node.type)) {
        throw new Error('Array selector match fields must be scalar values.');
    }

    return node.value;
}

function objectProperty(node, name, context) {
    if (node.type !== 'object') {
        throw new Error(`${context} requires an object.`);
    }
    const property = node.properties.get(name);

    if (!property) {
        throw new Error(`${context} could not find property ${JSON.stringify(name)}.`);
    }

    return property.value;
}

function resolvePath(node, path, context) {
    let current = node;

    for (const segment of path) {
        if (typeof segment === 'string') {
            current = objectProperty(current, segment, context);
            continue;
        }

        if (
            !segment ||
            typeof segment !== 'object' ||
            Array.isArray(segment) ||
            !segment.match ||
            typeof segment.match !== 'object' ||
            Array.isArray(segment.match)
        ) {
            throw new Error(`${context} has an unsupported path segment.`);
        }

        if (current.type !== 'array') {
            throw new Error(`${context} selector requires an array.`);
        }
        const matches = current.items.filter((candidate) => {
            if (candidate.type !== 'object') {
                return false;
            }

            return Object.entries(segment.match).every(([key, expected]) => {
                const property = candidate.properties.get(key);
                return property && scalarValue(property.value) === expected;
            });
        });

        if (matches.length !== 1) {
            throw new Error(`${context} selector matched ${matches.length} items; expected exactly one.`);
        }
        current = matches[0];
    }

    return current;
}

function locateMutation(root, mutation, index) {
    const context = `Mutation ${index}`;

    if (!mutation || typeof mutation !== 'object' || typeof mutation.toolId !== 'string' || !Array.isArray(mutation.path)) {
        throw new Error(`${context} must declare toolId and path.`);
    }

    if (typeof mutation.expected !== 'string' || typeof mutation.value !== 'string') {
        throw new Error(`${context} supports only string expected/value fields.`);
    }
    const tools = objectProperty(root, 'tools', context);

    if (tools.type !== 'array') {
        throw new Error('Top-level tools must be an array.');
    }
    const toolMatches = tools.items.filter((tool) => {
        if (tool.type !== 'object') {
            return false;
        }
        const id = tool.properties.get('id');
        return id && id.value.type === 'string' && id.value.value === mutation.toolId;
    });

    if (toolMatches.length !== 1) {
        throw new Error(`${context} found ${toolMatches.length} tools with id ${JSON.stringify(mutation.toolId)}; expected exactly one.`);
    }
    const target = resolvePath(toolMatches[0], mutation.path, context);

    if (target.type !== 'string') {
        throw new Error(`${context} target must be a JSON string.`);
    }

    if (target.value !== mutation.expected) {
        throw new Error(`${context} expected ${JSON.stringify(mutation.expected)} but found ${JSON.stringify(target.value)}.`);
    }

    return { target, mutation, index };
}

function mutateProjectToolsSource(source, specification) {
    // Validate the mutation request.
    if (!specification || specification.schemaVersion !== 1 || !Array.isArray(specification.mutations)) {
        throw new Error('Mutation specification must use schemaVersion 1 and declare a mutations array.');
    }

    // Locate and verify every requested value.
    const root = parseJsonSource(source);
    const located = specification.mutations.map((mutation, index) => locateMutation(root, mutation, index));
    const ranges = new Set();

    for (const entry of located) {
        const range = `${entry.target.start}:${entry.target.end}`;

        if (ranges.has(range)) {
            throw new Error(`Duplicate mutations target source range ${range}.`);
        }
        ranges.add(range);
    }

    // Apply replacements from the end so earlier source offsets remain valid.
    const ordered = [...located].sort((left, right) => right.target.start - left.target.start);
    let output = source;

    for (const entry of ordered) {
        output = output.slice(0, entry.target.start) + JSON.stringify(entry.mutation.value) + output.slice(entry.target.end);
    }

    // Reparse and verify every replacement before returning the result.
    const verifiedRoot = parseJsonSource(output);

    for (const entry of located) {
        const verification = { ...entry.mutation, expected: entry.mutation.value };
        locateMutation(verifiedRoot, verification, entry.index);
    }

    return output;
}


module.exports = { mutateProjectToolsSource };
