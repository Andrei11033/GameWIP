// Regression tests for the repository-owned issue-area label policy.

'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { AREA_LABELS_BY_VALUE, applyIssueAreaLabel, readIssueFormField, targetAreaLabel } = require('./issue-area-labels.js');

const currentMappings = [
    ['CMake / build system', 'area:cmake'],
    ['FileSystem', 'area:filesystem'],
    ['IO', 'area:io'],
    ['Terminal', 'area:terminal'],
    ['Unicode', 'area:unicode'],
    ['Foundation shared systems', 'area:foundation'],
    ['Logger', 'area:logger'],
    ['Assert', 'area:assert'],
    ['TestSupport', 'area:test-support'],
    ['Input', 'area:input'],
    ['Action', 'area:action'],
    ['Window', 'area:window'],
    ['Window manager', 'area:window-manager'],
    ['Engine shared systems', 'area:engine'],
    ['Windows platform backend', 'area:platform-win32'],
    ['Math', 'area:math'],
    ['Rendering', 'area:rendering'],
    ['Simulation', 'area:simulation'],
    ['Audio', 'area:audio'],
    ['Tools / developer support', 'area:tools'],
    ['Documentation', 'area:docs'],
    ['GitHub / repository setup', 'area:github'],
    ['Roadmap / planning', 'area:roadmap'],
    ['Gameplay', 'area:gameplay'],
];

test('reads an issue-form field', () => assert.equal(readIssueFormField('### Area\nUnicode\n### Details\nX', 'Area'), 'Unicode'));

test('maps every current area selection to exactly one canonical label', () => {
    assert.deepEqual([...AREA_LABELS_BY_VALUE], currentMappings);
    for (const [value, label] of currentMappings) {
        assert.equal(targetAreaLabel(`### Area\n${value}`, []), label);
    }
});

test('keeps historical body values as read-only aliases', () => {
    assert.equal(targetAreaLabel('### Area\nBuild and tooling', []), 'area:cmake');
    assert.equal(targetAreaLabel('### Area\nGameplay roadmap', []), 'area:roadmap');
    assert.equal(targetAreaLabel('### Area\nTools/developer support', []), 'area:tools');
    assert.equal(targetAreaLabel('### Area\nGitHub/repository setup', []), 'area:github');
    assert.equal(targetAreaLabel('### Area\nEngine input/action/window', ['area:input']), 'area:input');
    assert.equal(targetAreaLabel('### Area\nEngine input/action/window', []), 'area:engine');
});

test('keeps Roadmap and Gameplay separate', () => {
    assert.equal(targetAreaLabel('### Area\nRoadmap / planning', []), 'area:roadmap');
    assert.equal(targetAreaLabel('### Area\nGameplay', []), 'area:gameplay');
});

test('an issue without a recognized Area field keeps existing area metadata', async () => {
    const calls = [];
    const github = {
        rest: {
            issues: {
                addLabels: async (request) => calls.push(['add', request]),
                removeLabel: async (request) => calls.push(['remove', request]),
            },
        },
    };
    const context = {
        issue: { number: 73 },
        payload: { issue: { body: 'No issue-form area field.', labels: [{ name: 'area:roadmap' }] } },
        repo: { owner: 'Andrei11033', repo: 'GameWIP' },
    };

    assert.equal(targetAreaLabel(context.payload.issue.body, ['area:roadmap']), undefined);
    await applyIssueAreaLabel({ github, context });
    assert.deepEqual(calls, []);
});
