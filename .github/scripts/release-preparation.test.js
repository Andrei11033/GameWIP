// Regression tests for repository-owned release preparation/finalization policy.

'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const {
    buildReleasePreparationPlan,
    compareReleaseVersions,
    findLatestReleaseVersion,
    parseProjectVersion,
    parseMilestoneReleaseMetadata,
    parseReleaseTag,
    parseReleaseVersion,
    planFinalizationArtifacts,
    planPreparationArtifacts,
    releaseBodyFromNotes,
    releaseNotesPath,
    releaseNotesTemplate,
    releaseNames,
    releasePullRequestBody,
    selectNextMilestone,
    validateMergedReleasePullRequest,
    validateMilestoneReadiness,
    validateReleaseNotesReady,
    validateRequiredChecks,
    validateTargetVersion,
} = require('./release-preparation');

const masterSha = '1111111111111111111111111111111111111111';
const requiredChecks = ['Validation / Build and Test', 'Validation / AddressSanitizer'];

function successfulChecks(sha = masterSha) {
    return Object.fromEntries(requiredChecks.map((name) => [name, { status: 'completed', conclusion: 'success', headSha: sha }]));
}

function releaseMilestone(overrides = {}) {
    return {
        title: 'R00 - Bootstrap',
        state: 'open',
        description: ['Release version: `0.0.1`', 'Release issue: `#11`'].join('\n'),
        ...overrides,
    };
}

function milestoneIssues(overrides = []) {
    return [
        { number: 11, state: 'open', title: 'Complete R00 release' },
        { number: 10, state: 'closed', title: 'Completed implementation' },
        ...overrides,
    ];
}

function automaticReleaseMilestone(overrides = {}) {
    return releaseMilestone({
        title: 'R07 - Combat Foundation',
        description: 'Release version: `0.7.0`',
        ...overrides,
    });
}

test('parses numeric release versions', () => {
    assert.deepEqual(parseReleaseVersion('0.0.1'), {
        text: '0.0.1',
        major: 0,
        minor: 0,
        patch: 1,
    });

    assert.deepEqual(parseReleaseVersion('1.0.0'), {
        text: '1.0.0',
        major: 1,
        minor: 0,
        patch: 0,
    });
});

test('rejects non-release and malformed versions', () => {
    for (const version of ['01.0.0', '0.1', '0.1.0-dev', '0.1.0+build']) {
        assert.throws(() => parseReleaseVersion(version), /Invalid release version/);
    }
});

test('parses R00 milestone release metadata', () => {
    const description = ['Roadmap status: `[-]`', '', 'Release version: `0.0.1`', 'Release issue: `#11`'].join('\n');

    assert.deepEqual(parseMilestoneReleaseMetadata(description), {
        version: {
            text: '0.0.1',
            major: 0,
            minor: 0,
            patch: 1,
        },
        releaseIssue: 11,
    });
});

test('parses milestone metadata without an explicit release issue', () => {
    const description = ['Release version: `0.1.0`', 'Completion goal:'].join('\r\n');

    assert.deepEqual(parseMilestoneReleaseMetadata(description), {
        version: {
            text: '0.1.0',
            major: 0,
            minor: 1,
            patch: 0,
        },
        releaseIssue: null,
    });
});

test('requires exactly one valid release version line', () => {
    assert.throws(() => parseMilestoneReleaseMetadata(null), /Milestone description must be a string/);

    assert.throws(() => parseMilestoneReleaseMetadata('Completion goal only'), /Expected exactly one Release version line, found 0/);

    assert.throws(
        () => parseMilestoneReleaseMetadata(['Release version: `0.0.1`', 'Release version: `0.1.0`'].join('\n')),
        /Expected exactly one Release version line, found 2/,
    );

    assert.throws(() => parseMilestoneReleaseMetadata('Release version: 0.0.1'), /Invalid Release version line/);

    assert.throws(() => parseMilestoneReleaseMetadata('Release version: `01.0.0`'), /Invalid release version/);
});

test('rejects duplicate and malformed release issues', () => {
    const versionLine = 'Release version: `0.0.1`';

    assert.throws(
        () => parseMilestoneReleaseMetadata([versionLine, 'Release issue: `#11`', 'Release issue: `#12`'].join('\n')),
        /Expected at most one Release issue line, found 2/,
    );

    for (const issueLine of ['Release issue: `#0`', 'Release issue: `11`', 'Release issue: `#abc`']) {
        assert.throws(() => parseMilestoneReleaseMetadata([versionLine, issueLine].join('\n')), /Invalid Release issue line/);
    }
});

test('compares release versions numerically', () => {
    const compare = (left, right) => compareReleaseVersions(parseReleaseVersion(left), parseReleaseVersion(right));

    assert.equal(compare('0.0.1', '0.0.2'), -1);
    assert.equal(compare('0.9.0', '0.10.0'), -1);
    assert.equal(compare('0.51.0', '1.0.0'), -1);
    assert.equal(compare('1.0.0', '1.0.0'), 0);
    assert.equal(compare('1.1.0', '1.0.9'), 1);
});

test('parses the authoritative root CMake version', () => {
    const cmake = ['# project(GameWIP VERSION 9.9.9)', 'project(', '    GameWIP', '    VERSION 0.0.1', '    LANGUAGES CXX', ')'].join('\n');

    assert.deepEqual(parseProjectVersion(cmake), parseReleaseVersion('0.0.1'));
    assert.throws(() => parseProjectVersion('project(Other VERSION 1.0.0)'), /found 0/);
    assert.throws(() => parseProjectVersion('project(GameWIP VERSION 0.0.1)\nproject(GameWIP VERSION 0.1.0)'), /found 2/);
    assert.throws(() => parseProjectVersion('project(GameWIP VERSION 0.1)'), /Invalid release version/);
});

test('parses release tags and selects the latest numeric release', () => {
    assert.deepEqual(parseReleaseTag('v0.10.0'), parseReleaseVersion('0.10.0'));
    assert.deepEqual(findLatestReleaseVersion(['documentation-preview', 'v0.9.0', 'v0.10.0', 'v0.2.5']), parseReleaseVersion('0.10.0'));
    assert.equal(findLatestReleaseVersion(['documentation-preview']), null);
    assert.throws(() => parseReleaseTag('0.1.0'), /Invalid release tag/);
    assert.throws(() => findLatestReleaseVersion(['v0.1.0-dev']), /Invalid release version/);
});

test('requires the milestone target to match CMake and exceed the latest release', () => {
    const targetVersion = parseReleaseVersion('0.1.0');
    assert.equal(
        validateTargetVersion({
            targetVersion,
            projectVersion: parseReleaseVersion('0.1.0'),
            latestVersion: parseReleaseVersion('0.0.2'),
        }),
        true,
    );
    assert.throws(
        () =>
            validateTargetVersion({
                targetVersion,
                projectVersion: parseReleaseVersion('0.1.1'),
                latestVersion: null,
            }),
        /does not match root project version/,
    );
    assert.throws(
        () =>
            validateTargetVersion({
                targetVersion,
                projectVersion: targetVersion,
                latestVersion: parseReleaseVersion('0.1.0'),
            }),
        /must be newer than latest release/,
    );
});

test('accepts only a ready active milestone with one open release issue', () => {
    const result = validateMilestoneReadiness({
        activeMilestone: 'R00 - Bootstrap',
        milestone: releaseMilestone(),
        issues: milestoneIssues(),
    });
    assert.equal(result.version.text, '0.0.1');
    assert.equal(result.releaseIssue.number, 11);

    assert.throws(
        () =>
            validateMilestoneReadiness({
                activeMilestone: 'R01 - Window, Input, and Action Foundation',
                milestone: releaseMilestone(),
                issues: milestoneIssues(),
            }),
        /is not active milestone/,
    );
    assert.throws(
        () =>
            validateMilestoneReadiness({
                activeMilestone: 'R00 - Bootstrap',
                milestone: releaseMilestone(),
                issues: milestoneIssues([{ number: 12, state: 'open', title: 'Unfinished work' }]),
            }),
        /open implementation issues: 12/,
    );
    assert.throws(
        () =>
            validateMilestoneReadiness({
                activeMilestone: 'R00 - Bootstrap',
                milestone: releaseMilestone(),
                issues: [
                    { number: 11, state: 'closed', title: 'Complete R00 release' },
                    { number: 10, state: 'closed', title: 'Completed implementation' },
                ],
            }),
        /Release issue #11 must remain open/,
    );
});

test('discovers a future milestone release issue without hard-coded metadata', () => {
    const byLabel = validateMilestoneReadiness({
        activeMilestone: 'R07 - Combat Foundation',
        milestone: automaticReleaseMilestone(),
        issues: [
            { number: 70, state: 'closed', title: 'combat: implementation work' },
            { number: 71, state: 'open', title: 'task: complete R07 release', labels: ['type:release'] },
        ],
    });
    assert.equal(byLabel.version.text, '0.7.0');
    assert.equal(byLabel.releaseIssue.number, 71);

    const byTitle = validateMilestoneReadiness({
        activeMilestone: 'R07 - Combat Foundation',
        milestone: automaticReleaseMilestone(),
        issues: [
            { number: 70, state: 'closed', title: 'combat: implementation work' },
            { number: 71, state: 'open', title: 'release: complete R07 release' },
        ],
    });
    assert.equal(byTitle.releaseIssue.number, 71);
});

test('rejects missing, duplicate, and closed automatically discovered release issues', () => {
    assert.throws(
        () =>
            validateMilestoneReadiness({
                activeMilestone: 'R07 - Combat Foundation',
                milestone: automaticReleaseMilestone(),
                issues: [{ number: 70, state: 'closed', title: 'combat: implementation work' }],
            }),
        /exactly one release issue.*none were found/,
    );

    assert.throws(
        () =>
            validateMilestoneReadiness({
                activeMilestone: 'R07 - Combat Foundation',
                milestone: automaticReleaseMilestone(),
                issues: [
                    { number: 71, state: 'open', title: 'release: complete R07 release' },
                    { number: 72, state: 'open', title: 'task: publish R07', labels: ['type:release'] },
                ],
            }),
        /found #71, #72/,
    );

    assert.throws(
        () =>
            validateMilestoneReadiness({
                activeMilestone: 'R07 - Combat Foundation',
                milestone: automaticReleaseMilestone(),
                issues: [{ number: 71, state: 'closed', title: 'release: complete R07 release' }],
            }),
        /Release issue #71 must remain open/,
    );
});

test('requires successful checks for the exact latest master commit', () => {
    assert.equal(
        validateRequiredChecks({
            masterSha,
            evaluatedSha: masterSha,
            requiredChecks,
            checks: successfulChecks(),
        }),
        true,
    );
    assert.throws(
        () =>
            validateRequiredChecks({
                masterSha,
                evaluatedSha: '2222222222222222222222222222222222222222',
                requiredChecks,
                checks: successfulChecks(),
            }),
        /is not latest master commit/,
    );
    assert.throws(
        () =>
            validateRequiredChecks({
                masterSha,
                evaluatedSha: masterSha,
                requiredChecks,
                checks: {
                    ...successfulChecks(),
                    [requiredChecks[1]]: { status: 'completed', conclusion: 'failure', headSha: masterSha },
                },
            }),
        /has not completed successfully/,
    );
});

test('selects the next roadmap milestone by title number', () => {
    const milestones = [
        { title: 'R02 - Math Foundation' },
        { title: 'R00 - Bootstrap' },
        { title: 'PV1 - Multiplayer Foundation' },
        { title: 'R01 - Window, Input, and Action Foundation' },
    ];
    assert.equal(selectNextMilestone(milestones, 'R00 - Bootstrap').title, 'R01 - Window, Input, and Action Foundation');
    assert.equal(
        selectNextMilestone([{ title: 'R08 - AI Foundation' }, { title: 'R07 - Combat Foundation' }], 'R07 - Combat Foundation').title,
        'R08 - AI Foundation',
    );
    assert.equal(selectNextMilestone([{ title: 'R52 - V1' }], 'R52 - V1'), null);
    assert.throws(() => selectNextMilestone(milestones, 'R03 - Missing successor'), /found 0/);
});

test('plans preparation idempotently and rejects conflicting retries', () => {
    const version = parseReleaseVersion('0.0.1');
    const names = releaseNames(version);
    const releaseHeadSha = '3333333333333333333333333333333333333333';
    assert.deepEqual(planPreparationArtifacts({ version, masterSha }).createBranch, true);

    const reusable = planPreparationArtifacts({
        version,
        masterSha,
        branches: [{ name: names.branchName, sha: releaseHeadSha }],
        pullRequests: [
            {
                headRefName: names.branchName,
                headSha: releaseHeadSha,
                baseRefName: 'master',
                mergeBaseSha: masterSha,
                state: 'open',
            },
        ],
    });
    assert.equal(reusable.createBranch, false);
    assert.equal(reusable.createPullRequest, false);
    assert.equal(reusable.reusePullRequest, true);

    assert.throws(
        () =>
            planPreparationArtifacts({
                version,
                masterSha,
                branches: [{ name: names.branchName, sha: '2222222222222222222222222222222222222222' }],
            }),
        /not .*masterSha|not 111111/,
    );
    assert.throws(
        () =>
            planPreparationArtifacts({
                version,
                masterSha,
                branches: [{ name: names.branchName, sha: masterSha }],
                pullRequests: [
                    {
                        headRefName: names.branchName,
                        headSha: masterSha,
                        baseRefName: 'master',
                        mergeBaseSha: masterSha,
                        state: 'open',
                    },
                    {
                        headRefName: names.branchName,
                        headSha: masterSha,
                        baseRefName: 'master',
                        mergeBaseSha: masterSha,
                        state: 'merged',
                    },
                ],
            }),
        /Multiple release pull requests/,
    );
});

test('plans finalization idempotently and preserves immutable tags', () => {
    const version = parseReleaseVersion('0.0.1');
    const names = releaseNames(version);
    const fresh = planFinalizationArtifacts({ version, releaseCommitSha: masterSha });
    assert.equal(fresh.createTag, true);
    assert.equal(fresh.createRelease, true);

    const reusable = planFinalizationArtifacts({
        version,
        releaseCommitSha: masterSha,
        tags: [{ name: names.tagName, annotated: true, targetSha: masterSha }],
        releases: [{ tagName: names.tagName, draft: false }],
    });
    assert.equal(reusable.reuseTag, true);
    assert.equal(reusable.reuseRelease, true);

    assert.throws(
        () =>
            planFinalizationArtifacts({
                version,
                releaseCommitSha: masterSha,
                tags: [{ name: names.tagName, annotated: true, targetSha: '2222222222222222222222222222222222222222' }],
            }),
        /not the expected immutable annotated release tag/,
    );
});

test('builds a complete read-only preparation plan', () => {
    const snapshot = {
        activeMilestone: 'R00 - Bootstrap',
        milestone: releaseMilestone(),
        issues: milestoneIssues(),
        cmakeContents: 'project(GameWIP VERSION 0.0.1 LANGUAGES CXX)',
        tagNames: [],
        masterSha,
        evaluatedSha: masterSha,
        requiredChecks,
        checks: successfulChecks(),
        milestones: [releaseMilestone(), { title: 'R01 - Window, Input, and Action Foundation' }],
        branches: [],
        pullRequests: [],
    };

    const plan = buildReleasePreparationPlan(snapshot);
    assert.equal(plan.version.text, '0.0.1');
    assert.equal(plan.releaseIssue.number, 11);
    assert.equal(plan.nextMilestone.title, 'R01 - Window, Input, and Action Foundation');
    assert.equal(plan.artifacts.createBranch, true);
    assert.equal(plan.artifacts.createPullRequest, true);
});

test('generates release notes without closing the release issue', () => {
    const version = parseReleaseVersion('0.0.1');
    const notes = releaseNotesTemplate({
        version,
        milestoneTitle: 'R00 - Bootstrap',
        releaseIssue: { number: 11 },
        nextMilestoneTitle: 'R01 - Window, Input, and Action Foundation',
    });

    assert.equal(releaseNotesPath(version), 'docs/releases/v0.0.1.md');
    assert.match(notes, /^# GameWIP v0\.0\.1 release notes/);
    assert.match(notes, /Release issue: #11/);
    assert.match(notes, /Next milestone: R01 - Window, Input, and Action Foundation/);

    const body = releaseBodyFromNotes(notes);
    assert.match(body, /# GameWIP v0\.0\.1 release notes/);
    assert.doesNotMatch(body, /- \[ \]/);

    assert.throws(() => validateReleaseNotesReady(notes), /validation-evidence placeholder/);
    const notesWithEvidence = notes.replace(
        'Paste the final validation commands, environment, results, skips, and observed manual UI behavior here before merging this release-preparation pull request.',
        'Final validation evidence was recorded in this pull request.',
    );
    assert.throws(() => validateReleaseNotesReady(notesWithEvidence), /unchecked release checklist/);

    const completedNotes = notesWithEvidence.replaceAll('- [ ]', '- [x]');
    assert.equal(validateReleaseNotesReady(completedNotes), true);
});

test('generates release pull request body for human merge', () => {
    const snapshot = {
        activeMilestone: 'R00 - Bootstrap',
        milestone: releaseMilestone(),
        issues: milestoneIssues(),
        cmakeContents: 'project(GameWIP VERSION 0.0.1 LANGUAGES CXX)',
        tagNames: [],
        masterSha,
        evaluatedSha: masterSha,
        requiredChecks,
        checks: successfulChecks(),
        milestones: [releaseMilestone(), { title: 'R01 - Window, Input, and Action Foundation' }],
        branches: [],
        pullRequests: [],
    };

    const body = releasePullRequestBody(buildReleasePreparationPlan(snapshot));
    assert.match(body, /release-preparation workflow/);
    assert.match(body, /Refs #11/);
    assert.doesNotMatch(body, /Closes #11/);
    assert.doesNotMatch(body, /when R00 is ready/);
    assert.match(body, /when this release is ready/);
    assert.match(body, /human review and merge/);
});

test('finalization requires the merged release pull request commit', () => {
    const version = parseReleaseVersion('0.0.1');
    const merged = planPreparationArtifacts({
        version,
        masterSha,
        branches: [],
        pullRequests: [
            {
                number: 42,
                headRefName: releaseNames(version).branchName,
                headSha: '3333333333333333333333333333333333333333',
                baseRefName: 'master',
                mergeBaseSha: masterSha,
                mergeCommitSha: masterSha,
                state: 'merged',
            },
        ],
    });
    assert.equal(validateMergedReleasePullRequest({ artifacts: merged }, masterSha), true);

    const open = planPreparationArtifacts({
        version,
        masterSha,
        branches: [{ name: releaseNames(version).branchName, sha: masterSha }],
        pullRequests: [
            {
                number: 42,
                headRefName: releaseNames(version).branchName,
                headSha: masterSha,
                baseRefName: 'master',
                mergeBaseSha: masterSha,
                mergeCommitSha: null,
                state: 'open',
            },
        ],
    });
    assert.throws(() => validateMergedReleasePullRequest({ artifacts: open }, masterSha), /must be merged/);
    assert.throws(
        () => validateMergedReleasePullRequest({ artifacts: { ...merged, pullRequestMergeCommitSha: masterSha } }, '2222'),
        /is not release commit/,
    );
});
