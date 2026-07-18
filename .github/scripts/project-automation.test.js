'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const {
    STATUS,
    deriveIssueStatus,
    derivePullRequestStatus,
    hasRequiredLabels,
    isPullRequestIssueEvent,
    mergeLinkedIssueMetadata,
    readConfig,
    splitRepository,
} = require('./project-automation');

const activeMilestone = 'R00 - Bootstrap';

function issue(overrides = {}) {
    return {
        state: 'OPEN',
        openBlockerCount: 0,
        linkedPullRequests: [],
        assignees: [],
        milestone: activeMilestone,
        labels: ['area:github', 'type:task', 'priority:normal'],
        ...overrides,
    };
}

test('issue status priority is done, blocked, review, in progress, ready, backlog', () => {
    assert.equal(deriveIssueStatus(issue({state: 'CLOSED', openBlockerCount: 1}), activeMilestone), STATUS.DONE);
    assert.equal(
        deriveIssueStatus(
            issue({openBlockerCount: 1, linkedPullRequests: [{state: 'OPEN', isDraft: false, reviewDecision: null}]}),
            activeMilestone,
        ),
        STATUS.BLOCKED,
    );
    assert.equal(
        deriveIssueStatus(issue({linkedPullRequests: [{state: 'OPEN', isDraft: false, reviewDecision: null}]}), activeMilestone),
        STATUS.REVIEW,
    );
    assert.equal(
        deriveIssueStatus(
            issue({linkedPullRequests: [{state: 'OPEN', isDraft: false, reviewDecision: 'CHANGES_REQUESTED'}]}),
            activeMilestone,
        ),
        STATUS.IN_PROGRESS,
    );
    assert.equal(deriveIssueStatus(issue({assignees: ['Andrei11033']}), activeMilestone), STATUS.IN_PROGRESS);
    assert.equal(deriveIssueStatus(issue(), activeMilestone), STATUS.READY);
    assert.equal(deriveIssueStatus(issue({milestone: 'R01 - Core Engine'}), activeMilestone), STATUS.BACKLOG);
});

test('ready issues require area, type, and priority labels', () => {
    assert.equal(hasRequiredLabels(['area:github', 'type:task', 'priority:normal']), true);
    assert.equal(hasRequiredLabels(['area:github', 'type:task']), false);
    assert.equal(deriveIssueStatus(issue({labels: ['area:github', 'type:task']}), activeMilestone), STATUS.BACKLOG);
});

test('pull request status follows closure, draft state, and review decision', () => {
    assert.equal(derivePullRequestStatus({state: 'MERGED', isDraft: false, reviewDecision: null}), STATUS.DONE);
    assert.equal(derivePullRequestStatus({state: 'CLOSED', isDraft: false, reviewDecision: null}), STATUS.DONE);
    assert.equal(derivePullRequestStatus({state: 'OPEN', isDraft: true, reviewDecision: null}), STATUS.IN_PROGRESS);
    assert.equal(
        derivePullRequestStatus({state: 'OPEN', isDraft: false, reviewDecision: 'CHANGES_REQUESTED'}),
        STATUS.IN_PROGRESS,
    );
    assert.equal(derivePullRequestStatus({state: 'OPEN', isDraft: false, reviewDecision: 'APPROVED'}), STATUS.REVIEW);
});

test('linked issue metadata is additive and chooses the highest priority', () => {
    const metadata = mergeLinkedIssueMetadata(
        [
            {
                labels: ['area:github', 'type:task', 'priority:normal'],
                assignees: ['Andrei11033'],
                milestone: {number: 122, title: activeMilestone},
            },
            {
                labels: ['area:cmake', 'type:tooling', 'priority:high'],
                assignees: [],
                milestone: {number: 122, title: activeMilestone},
            },
        ],
        'author',
    );

    assert.deepEqual(metadata.labels, ['area:cmake', 'area:github', 'priority:high', 'type:task', 'type:tooling']);
    assert.deepEqual(metadata.assignees, ['Andrei11033']);
    assert.deepEqual(metadata.milestone, {number: 122, title: activeMilestone});
    assert.deepEqual(metadata.milestoneConflict, []);
});

test('metadata falls back to the PR author and reports milestone conflicts', () => {
    const metadata = mergeLinkedIssueMetadata(
        [
            {labels: [], assignees: [], milestone: {number: 1, title: 'R00'}},
            {labels: [], assignees: [], milestone: {number: 2, title: 'R01'}},
        ],
        'author',
    );

    assert.deepEqual(metadata.assignees, ['author']);
    assert.equal(metadata.milestone, null);
    assert.deepEqual(metadata.milestoneConflict, ['R00', 'R01']);
});

test('issues events distinguish pull requests from issues', () => {
    assert.equal(
        isPullRequestIssueEvent({
            eventName: 'issues',
            payload: {issue: {number: 23, pull_request: {url: 'https://api.github.com/repos/owner/repo/pulls/23'}}},
        }),
        true,
    );
    assert.equal(isPullRequestIssueEvent({eventName: 'issues', payload: {issue: {number: 24}}}), false);
    assert.equal(
        isPullRequestIssueEvent({eventName: 'pull_request_target', payload: {issue: {number: 23, pull_request: {}}}}),
        false,
    );
});

test('configuration and repository names are validated', () => {
    assert.deepEqual(splitRepository('Andrei11033/GameWIP'), {owner: 'Andrei11033', repository: 'GameWIP'});
    assert.throws(() => splitRepository('GameWIP'), /Invalid repository name/);

    const config = readConfig(
        {payload: {repository: {full_name: 'Andrei11033/GameWIP'}}},
        {PROJECT_OWNER: 'Andrei11033', PROJECT_NUMBER: '2', ACTIVE_MILESTONE: activeMilestone},
    );
    assert.equal(config.projectNumber, 2);
    assert.equal(config.dryRun, false);
    assert.throws(() => readConfig({payload: {}}, {}), /must be configured/);
});
