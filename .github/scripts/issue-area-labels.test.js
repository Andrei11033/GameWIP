// Regression tests for the repository-owned issue-area label policy.

'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { readIssueFormField, targetAreaLabel } = require('./issue-area-labels.js');

test('reads an issue-form field', () => assert.equal(readIssueFormField('### Area\nUnicode\n### Details\nX', 'Area'), 'Unicode'));
test('maps current and legacy areas', () => {
    assert.equal(targetAreaLabel('### Area\nWindow', []), 'area:window');
    assert.equal(targetAreaLabel('### Area\nEngine input/action/window', ['area:input']), 'area:input');
});
