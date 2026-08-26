'use strict';

const fs = require('fs');
const { TextDecoder } = require('util');

class JsonSourceParser {
    constructor(source) {
        this.source = source;
        this.offset = 0;
    }

    parse() {
        this.skipWhitespace();
        const value = this.parseValue();
        this.skipWhitespace();
        if (this.offset !== this.source.length) {
            this.fail('unexpected trailing content');
        }
        return value;
    }

    fail(message) {
        throw new Error(`Malformed JSON at offset ${this.offset}: ${message}.`);
    }

    skipWhitespace() {
        while (this.offset < this.source.length && ' \t\r\n'.includes(this.source[this.offset])) {
            this.offset += 1;
        }
    }

    parseValue() {
        const character = this.source[this.offset];
        if (character === '{') {
            return this.parseObject();
        }
        if (character === '[') {
            return this.parseArray();
        }
        if (character === '"') {
            return this.parseString();
        }
        if (character === 't') {
            return this.parseLiteral('true', true);
        }
        if (character === 'f') {
            return this.parseLiteral('false', false);
        }
        if (character === 'n') {
            return this.parseLiteral('null', null);
        }
        return this.parseNumber();
    }

    parseString() {
        const start = this.offset++;
        while (this.offset < this.source.length) {
            const character = this.source[this.offset++];
            if (character === '"') {
                const token = this.source.slice(start, this.offset);
                let value;
                try {
                    value = JSON.parse(token);
                } catch (error) {
                    this.fail(error.message);
                }
                return { type: 'string', value, start, end: this.offset };
            }
            if (character === '\\') {
                if (this.offset >= this.source.length) {
                    this.fail('unterminated escape sequence');
                }
                this.offset += 1;
            } else if (character.charCodeAt(0) < 0x20) {
                this.fail('unescaped control character in string');
            }
        }
        this.fail('unterminated string');
    }

    parseObject() {
        const start = this.offset++;
        const properties = new Map();
        this.skipWhitespace();
        if (this.source[this.offset] === '}') {
            this.offset += 1;
            return { type: 'object', properties, start, end: this.offset };
        }
        while (true) {
            if (this.source[this.offset] !== '"') {
                this.fail('object key must be a string');
            }
            const key = this.parseString();
            if (properties.has(key.value)) {
                this.fail(`duplicate object key ${JSON.stringify(key.value)}`);
            }
            this.skipWhitespace();
            if (this.source[this.offset++] !== ':') {
                this.fail("expected ':' after object key");
            }
            this.skipWhitespace();
            properties.set(key.value, { key, value: this.parseValue() });
            this.skipWhitespace();
            const separator = this.source[this.offset++];
            if (separator === '}') {
                break;
            }
            if (separator !== ',') {
                this.fail("expected ',' or '}' in object");
            }
            this.skipWhitespace();
        }
        return { type: 'object', properties, start, end: this.offset };
    }

    parseArray() {
        const start = this.offset++;
        const items = [];
        this.skipWhitespace();
        if (this.source[this.offset] === ']') {
            this.offset += 1;
            return { type: 'array', items, start, end: this.offset };
        }
        while (true) {
            items.push(this.parseValue());
            this.skipWhitespace();
            const separator = this.source[this.offset++];
            if (separator === ']') {
                break;
            }
            if (separator !== ',') {
                this.fail("expected ',' or ']' in array");
            }
            this.skipWhitespace();
        }
        return { type: 'array', items, start, end: this.offset };
    }

    parseLiteral(token, value) {
        const start = this.offset;
        if (this.source.slice(this.offset, this.offset + token.length) !== token) {
            this.fail('invalid literal');
        }
        this.offset += token.length;
        return { type: value === null ? 'null' : 'boolean', value, start, end: this.offset };
    }

    parseNumber() {
        const start = this.offset;
        const remaining = this.source.slice(this.offset);
        const match = /^-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?/.exec(remaining);
        if (!match) {
            this.fail('expected a JSON value');
        }
        this.offset += match[0].length;
        return { type: 'number', value: Number(match[0]), start, end: this.offset };
    }
}

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

function parseJsonSource(source) {
    return new JsonSourceParser(source).parse();
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
    if (!specification || specification.schemaVersion !== 1 || !Array.isArray(specification.mutations)) {
        throw new Error('Mutation specification must use schemaVersion 1 and declare a mutations array.');
    }
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
    const ordered = [...located].sort((left, right) => right.target.start - left.target.start);
    let output = source;
    for (const entry of ordered) {
        output = output.slice(0, entry.target.start) + JSON.stringify(entry.mutation.value) + output.slice(entry.target.end);
    }
    const verifiedRoot = parseJsonSource(output);
    for (const entry of located) {
        const verification = { ...entry.mutation, expected: entry.mutation.value };
        locateMutation(verifiedRoot, verification, entry.index);
    }
    return output;
}

function readUtf8(path) {
    const bytes = fs.readFileSync(path);
    if (bytes.length >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) {
        throw new Error(`${path} must be UTF-8 without BOM.`);
    }
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
}

function runCli(argv) {
    if (argv.length !== 3) {
        throw new Error('Usage: project-tools-json-mutator.js <source-json> <mutation-spec-json> <output-json>');
    }
    const [sourcePath, specificationPath, outputPath] = argv;
    const source = readUtf8(sourcePath);
    const specification = JSON.parse(readUtf8(specificationPath));
    fs.writeFileSync(outputPath, mutateProjectToolsSource(source, specification), { encoding: 'utf8' });
}

module.exports = { mutateProjectToolsSource, parseJsonSource, runCli };

if (require.main === module) {
    try {
        runCli(process.argv.slice(2));
    } catch (error) {
        process.stderr.write(`${error.message}\n`);
        process.exitCode = 1;
    }
}
