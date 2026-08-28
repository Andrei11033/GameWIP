// Regression tests for structure-preserving project-tool registry updates.

'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { TextDecoder } = require('util');
const { mutateProjectToolsSource } = require('../../scripts/lib/project-tools-json-mutator.js');

const source = `{
  "tools": [
    {
      "name": "ESLint's \\u0063onfig",
      "id": "eslint",
      "provider": {
        "dependencies": [
          { "version": "10.0.1", "package": "@eslint/js", "note": "caf\\u00e9" },
          { "package": "other", "version": "1.0.0" }
        ],
        "kind": "npm"
      },
      "requiredVersion": "10.9.0"
    },
    {
      "provider": {
        "assets": {
          "windows-amd64": { "sha256": "old-win", "archive": "old-win.zip" },
          "linux-amd64": { "archive": "old-linux.tgz", "sha256": "old-linux" }
        },
        "releaseTag": "v1.0.0",
        "kind": "githubRelease"
      },
      "requiredVersion": "1.0.0",
      "id": "actionlint"
    }
  ],
  "schemaVersion": 1,
  "unrelated": "slash\\/quote\\"/unicode \\u2026"
}
`;

function specification(mutations) {
    return { schemaVersion: 1, mutations };
}

function mutation(toolId, pathSegments, expected, value) {
    return { toolId, path: pathSegments, expected, value };
}

function assertOnlyTokenChanged(before, after, oldToken, newToken) {
    const offset = before.indexOf(oldToken);
    assert.notStrictEqual(offset, -1);
    assert.strictEqual(after, before.slice(0, offset) + newToken + before.slice(offset + oldToken.length));
}

{
    const output = mutateProjectToolsSource(source, specification([mutation('eslint', ['requiredVersion'], '10.9.0', '10.9.1')]));
    assertOnlyTokenChanged(source, output, '"10.9.0"', '"10.9.1"');
    assert(output.includes('"schemaVersion": 1'));
    assert(output.includes('"name": "ESLint\'s \\u0063onfig"'));
    assert(output.includes('"note": "caf\\u00e9"'));
    assert(output.includes('"unrelated": "slash\\/quote\\"/unicode \\u2026"'));
}

{
    const mutations = [
        mutation('eslint', ['requiredVersion'], '10.9.0', '10.9.1'),
        mutation('eslint', ['provider', 'dependencies', { match: { package: '@eslint/js' } }, 'version'], '10.0.1', '10.0.2'),
        mutation('actionlint', ['requiredVersion'], '1.0.0', '1.1.0'),
        mutation('actionlint', ['provider', 'releaseTag'], 'v1.0.0', 'v1.1.0'),
        mutation('actionlint', ['provider', 'assets', 'windows-amd64', 'archive'], 'old-win.zip', 'new-win.zip'),
        mutation('actionlint', ['provider', 'assets', 'windows-amd64', 'sha256'], 'old-win', 'new-win'),
    ];
    const output = mutateProjectToolsSource(source, specification(mutations));
    assert.strictEqual(JSON.parse(output).tools[0].provider.dependencies[0].version, '10.0.2');
    assert.strictEqual(JSON.parse(output).tools[1].provider.assets['windows-amd64'].sha256, 'new-win');
    assert(output.indexOf('"id": "eslint"') < output.indexOf('"id": "actionlint"'));
    assert(output.indexOf('"version": "10.0.2"') < output.indexOf('"package": "@eslint/js"'));
    assert(output.indexOf('"windows-amd64"') < output.indexOf('"linux-amd64"'));
    assert(output.indexOf('"tools"') < output.indexOf('"schemaVersion"'));
}

assert.throws(
    () =>
        mutateProjectToolsSource(
            source,
            specification([mutation('eslint', ['provider', 'dependencies', { match: { package: 'missing' } }, 'version'], '1', '2')]),
        ),
    /matched 0 items/,
);

{
    const ambiguous = source.replace('{ "package": "other", "version": "1.0.0" }', '{ "package": "@eslint/js", "version": "1.0.0" }');
    assert.throws(
        () =>
            mutateProjectToolsSource(
                ambiguous,
                specification([
                    mutation('eslint', ['provider', 'dependencies', { match: { package: '@eslint/js' } }, 'version'], '10.0.1', '10.0.2'),
                ]),
            ),
        /matched 2 items/,
    );
}

assert.throws(
    () => mutateProjectToolsSource(source, specification([mutation('eslint', ['requiredVersion'], 'stale', '10.9.1')])),
    /expected "stale" but found "10.9.0"/,
);

assert.throws(
    () =>
        mutateProjectToolsSource(
            source,
            specification([mutation('eslint', ['requiredVersion'], '10.9.0', '10.9.1'), mutation('eslint', ['requiredVersion'], '10.9.0', '10.9.2')]),
        ),
    /Duplicate mutations/,
);

assert.throws(() => mutateProjectToolsSource('{"tools":[],"tools":[]}', specification([])), /duplicate object key/);

assert.strictEqual(mutateProjectToolsSource(source, specification([])), source);

{
    const temporaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'gamewip-json-mutator-'));
    try {
        const sourcePath = path.join(temporaryRoot, 'source.json');
        const specificationPath = path.join(temporaryRoot, 'mutations.json');
        const outputPath = path.join(temporaryRoot, 'output.json');
        fs.writeFileSync(sourcePath, source, 'utf8');
        fs.writeFileSync(specificationPath, JSON.stringify(specification([mutation('eslint', ['requiredVersion'], '10.9.0', '10.9.1')])), 'utf8');
        require('child_process').execFileSync(process.execPath, [
            path.join(__dirname, '../../scripts/lib/project-tools-json-mutator.js'),
            sourcePath,
            specificationPath,
            outputPath,
        ]);
        const bytes = fs.readFileSync(outputPath);
        assert.notDeepStrictEqual([...bytes.subarray(0, 3)], [0xef, 0xbb, 0xbf]);
        assert.strictEqual(new TextDecoder('utf-8', { fatal: true }).decode(bytes).includes('"requiredVersion": "10.9.1"'), true);
    } finally {
        fs.rmSync(temporaryRoot, { recursive: true, force: true });
    }
}

process.stdout.write('project-tools JSON mutator tests passed.\n');
