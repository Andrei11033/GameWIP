// Update selected values in the project-tool registry without reformatting it.

'use strict';

const { parseJsonSource } = require('./project-tools-json-mutator/source_parser');
const { mutateProjectToolsSource } = require('./project-tools-json-mutator/mutations');
const { runCli } = require('./project-tools-json-mutator/cli');

module.exports = { mutateProjectToolsSource, parseJsonSource, runCli };

if (require.main === module) {
    try {
        runCli(process.argv.slice(2));
    } catch (error) {
        process.stderr.write(`${error.message}\n`);
        process.exitCode = 1;
    }
}