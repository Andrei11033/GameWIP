// Regression tests for the repository-owned pull-request metadata policy.

'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const test = require('node:test');
const path = require('node:path');
const { hasMeaningfulContent, validatePullRequest } = require('./pr-standards.js');

const valid = {
    title: 'github: standardize repository policy',
    body: '## Summary\nStandardizes policy.\n## Validation\nRan tests.',
    labels: [{ name: 'area:github' }, { name: 'type:task' }, { name: 'priority:normal' }],
};

test('accepts exactly one canonical primary label per dimension', () => assert.deepEqual(validatePullRequest(valid), []));

test('accepts optional compat:breaking', () => {
    assert.deepEqual(validatePullRequest({ ...valid, labels: [...valid.labels, { name: 'compat:breaking' }] }), []);
});

test('accepts optional issue and implementation sections', () => {
    const body = `${valid.body}\n## Linked Issues\nNo linked issue: maintenance\n## Implementation Notes\nKeep the policy focused.`;
    assert.deepEqual(validatePullRequest({ ...valid, body }), []);
});

test('accepts an empty optional issue section', () => {
    const body = `${valid.body}\n## Linked Issues\n<!-- None for this maintenance change. -->`;
    assert.deepEqual(validatePullRequest({ ...valid, body }), []);
});

test('rejects a malformed populated issue section', () => {
    const body = `${valid.body}\n## Linked Issues\nIssue discussed elsewhere.`;
    assert.ok(validatePullRequest({ ...valid, body }).some((error) => error.includes('Closes #123')));
});

test('accepts the actual PR template shape', () => {
    const template = fs.readFileSync(path.join(__dirname, '..', 'PULL_REQUEST_TEMPLATE.md'), 'utf8');
    const body = template
        .replace('<!-- Record checks you ran and their results. -->', '`gamewip.bat quality check` passed.')
        .replace('<!-- Briefly describe what changed and why. -->', 'Standardizes the repository policy.');
    assert.deepEqual(validatePullRequest({ ...valid, body }), []);
});

test('rejects the untouched PR template placeholders', () => {
    const template = fs.readFileSync(path.join(__dirname, '..', 'PULL_REQUEST_TEMPLATE.md'), 'utf8');
    const errors = validatePullRequest({ ...valid, body: template });
    assert.ok(errors.includes('`## Summary` must contain meaningful content.'));
    assert.ok(errors.includes('`## Validation` must contain meaningful content.'));
});

test('does not require removed convenience sections', () => {
    assert.deepEqual(validatePullRequest(valid), []);
});

test('accepts every canonical type and priority', () => {
    for (const type of ['type:bug', 'type:feature', 'type:task', 'type:decision', 'type:release']) {
        for (const priority of ['priority:high', 'priority:normal', 'priority:low']) {
            const labels = [{ name: 'area:github' }, { name: type }, { name: priority }];
            assert.deepEqual(validatePullRequest({ ...valid, labels }), []);
        }
    }
});

test('rejects missing and duplicate primary metadata', () => {
    for (const prefix of ['area:', 'type:', 'priority:']) {
        const missing = valid.labels.filter((label) => !label.name.startsWith(prefix));
        assert.ok(validatePullRequest({ ...valid, labels: missing }).some((error) => error.includes(`exactly one \`${prefix}*\``)));
    }

    assert.ok(
        validatePullRequest({ ...valid, labels: [...valid.labels, { name: 'area:tools' }] }).some((error) => error.includes('exactly one `area:*`')),
    );
    assert.ok(
        validatePullRequest({ ...valid, labels: [...valid.labels, { name: 'type:feature' }] }).some((error) =>
            error.includes('exactly one `type:*`'),
        ),
    );
    assert.ok(
        validatePullRequest({ ...valid, labels: [...valid.labels, { name: 'priority:high' }] }).some((error) =>
            error.includes('exactly one `priority:*`'),
        ),
    );
});

test('rejects unsupported metadata values', () => {
    const unknownType = valid.labels.map((label) => (label.name === 'type:task' ? { name: 'type:unknown' } : label));
    assert.ok(validatePullRequest({ ...valid, labels: unknownType }).some((error) => error.includes('type:unknown')));

    const underscoreArea = valid.labels.map((label) => (label.name === 'area:github' ? { name: 'area:invalid_area' } : label));
    assert.ok(validatePullRequest({ ...valid, labels: underscoreArea }).some((error) => error.includes('not canonical')));

    const unsupportedPriority = valid.labels.map((label) => (label.name === 'priority:normal' ? { name: 'priority:urgent' } : label));
    assert.ok(validatePullRequest({ ...valid, labels: unsupportedPriority }).some((error) => error.includes('not supported')));

    assert.ok(
        validatePullRequest({ ...valid, labels: [...valid.labels, { name: 'compat:compatible' }] }).some((error) =>
            error.includes('compat:compatible'),
        ),
    );
});

test('rejects a placeholder pull-request title', () => {
    const errors = validatePullRequest({ ...valid, title: 'area: imperative summary' });
    assert.ok(errors.some((error) => error.includes('PR title must use')));
});

test('does not let removed HTML comments reform comment openers', () => {
    assert.equal(hasMeaningfulContent('<<!-- ignored -->!--'), false);
    assert.equal(hasMeaningfulContent('<!-- unterminated'), false);
    assert.equal(hasMeaningfulContent('meaningful<!-- ignored -->'), true);
});
