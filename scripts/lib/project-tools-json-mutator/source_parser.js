'use strict';

// Source-preserving JSON parser.

// ------------------------------------------------------------
// Parse and retain source offsets
// ------------------------------------------------------------

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

function parseJsonSource(source) {
    return new JsonSourceParser(source).parse();
}

module.exports = { parseJsonSource };
