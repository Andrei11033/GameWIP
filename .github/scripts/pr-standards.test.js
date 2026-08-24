// Regression tests for the repository-owned pull-request metadata policy.

'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { validatePullRequest } = require('./pr-standards.js');

const valid = {
    title: 'github: standardize repository policy',
    body: '## Summary\nStandardizes policy.\n## Linked Issues\nNo linked issue: maintenance\n## Validation\nRan tests.\n## Merge Message\nTitle: github: standardize repository policy\nBody: Keep policy tested.\n## Checklist\n- [x] Done',
    labels: [{ name: 'area:github' }, { name: 'type:tooling' }, { name: 'priority:normal' }],
};

test('accepts complete metadata', () => assert.deepEqual(validatePullRequest(valid), []));
test('rejects placeholders and missing labels', () => {
    const body = valid.body.replace('github: standardize repository policy', 'area: imperative summary');
    const errors = validatePullRequest({ ...valid, body, labels: [] });
    assert.ok(errors.some((error) => error.includes('concrete')));
    assert.equal(errors.filter((error) => error.includes('label')).length, 3);
});
