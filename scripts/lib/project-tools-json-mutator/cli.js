'use strict';

const fs = require('fs');
const { TextDecoder } = require('util');
const { mutateProjectToolsSource } = require('./mutations');

// ------------------------------------------------------------
// Read UTF-8 and run the command-line entry point
// ------------------------------------------------------------

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
module.exports = { runCli };
