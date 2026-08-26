// Regression tests for the repository-owned pull-request metadata policy.

'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { hasMeaningfulContent, validatePullRequest } = require('./pr-standards.js');

const valid = {
    title: 'github: standardize repository policy',
    body: '## Summary\nStandardizes policy.\n## Linked Issues\nNo linked issue: maintenance\n## Validation\nRan tests.\n## Merge Message\nTitle: github: standardize repository policy\nBody: Keep policy tested.\n## Checklist\n- [x] Done',
    labels: [{ name: 'area:github' }, { name: 'type:task' }, { name: 'priority:normal' }],
};

test('accepts exactly one canonical primary label per dimension', () => assert.deepEqual(validatePullRequest(valid), []));

test('accepts optional compat:breaking', () => {
    assert.deepEqual(validatePullRequest({ ...valid, labels: [...valid.labels, { name: 'compat:breaking' }] }), []);
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

test('rejects retired or unsupported metadata values', () => {
    const tooling = valid.labels.map((label) => (label.name === 'type:task' ? { name: 'type:tooling' } : label));
    assert.ok(validatePullRequest({ ...valid, labels: tooling }).some((error) => error.includes('type:tooling')));

    const underscoreArea = valid.labels.map((label) => (label.name === 'area:github' ? { name: 'area:test_support' } : label));
    assert.ok(validatePullRequest({ ...valid, labels: underscoreArea }).some((error) => error.includes('not canonical')));

    const unsupportedPriority = valid.labels.map((label) => (label.name === 'priority:normal' ? { name: 'priority:urgent' } : label));
    assert.ok(validatePullRequest({ ...valid, labels: unsupportedPriority }).some((error) => error.includes('not supported')));

    assert.ok(
        validatePullRequest({ ...valid, labels: [...valid.labels, { name: 'compat:compatible' }] }).some((error) =>
            error.includes('compat:compatible'),
        ),
    );
});

test('rejects placeholders while preserving normal body validation', () => {
    const body = valid.body.replace('github: standardize repository policy', 'area: imperative summary');
    assert.ok(validatePullRequest({ ...valid, body }).some((error) => error.includes('concrete')));
});

test('does not let removed HTML comments reform comment openers', () => {
    assert.equal(hasMeaningfulContent('<<!-- ignored -->!--'), false);
    assert.equal(hasMeaningfulContent('<!-- unterminated'), false);
    assert.equal(hasMeaningfulContent('meaningful<!-- ignored -->'), true);
});
