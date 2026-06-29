'use strict';

const STATUS = Object.freeze({
    BACKLOG: 'Backlog',
    READY: 'Ready',
    IN_PROGRESS: 'In Progress',
    REVIEW: 'Review',
    BLOCKED: 'Blocked',
    DONE: 'Done',
});

const REQUIRED_LABEL_PREFIXES = Object.freeze(['area:', 'type:', 'priority:']);
const PRIORITY_ORDER = Object.freeze({
    'priority:high': 3,
    'priority:normal': 2,
    'priority:low': 1,
});

function hasRequiredLabels(labels) {
    return REQUIRED_LABEL_PREFIXES.every((prefix) => labels.some((label) => label.startsWith(prefix)));
}

function deriveIssueStatus(issue, activeMilestone) {
    if (issue.state !== 'OPEN') {
        return STATUS.DONE;
    }
    if (issue.openBlockerCount > 0) {
        return STATUS.BLOCKED;
    }

    const openPullRequests = issue.linkedPullRequests.filter((pullRequest) => pullRequest.state === 'OPEN');
    const reviewablePullRequest = openPullRequests.some(
        (pullRequest) => !pullRequest.isDraft && pullRequest.reviewDecision !== 'CHANGES_REQUESTED',
    );
    if (reviewablePullRequest) {
        return STATUS.REVIEW;
    }
    if (openPullRequests.length > 0 || issue.assignees.length > 0) {
        return STATUS.IN_PROGRESS;
    }
    if (issue.milestone === activeMilestone && hasRequiredLabels(issue.labels)) {
        return STATUS.READY;
    }
    return STATUS.BACKLOG;
}

function derivePullRequestStatus(pullRequest) {
    if (pullRequest.state !== 'OPEN') {
        return STATUS.DONE;
    }
    if (pullRequest.isDraft || pullRequest.reviewDecision === 'CHANGES_REQUESTED') {
        return STATUS.IN_PROGRESS;
    }
    return STATUS.REVIEW;
}

function mergeLinkedIssueMetadata(linkedIssues, author) {
    const labels = new Set();
    const priorities = new Set();
    const assignees = new Set();
    const milestones = new Map();

    for (const issue of linkedIssues) {
        for (const label of issue.labels) {
            if (label.startsWith('priority:')) {
                priorities.add(label);
            } else if (label.startsWith('area:') || label.startsWith('type:')) {
                labels.add(label);
            }
        }
        for (const assignee of issue.assignees) {
            assignees.add(assignee);
        }
        if (issue.milestone !== null) {
            milestones.set(issue.milestone.number, issue.milestone);
        }
    }

    const selectedPriority = [...priorities].sort(
        (left, right) => (PRIORITY_ORDER[right] ?? 0) - (PRIORITY_ORDER[left] ?? 0),
    )[0];
    if (selectedPriority !== undefined) {
        labels.add(selectedPriority);
    }
    if (assignees.size === 0 && author) {
        assignees.add(author);
    }

    return {
        labels: [...labels].sort(),
        assignees: [...assignees].sort(),
        milestone: milestones.size === 1 ? [...milestones.values()][0] : null,
        milestoneConflict: milestones.size > 1 ? [...milestones.values()].map((milestone) => milestone.title).sort() : [],
    };
}

function splitRepository(nameWithOwner) {
    const [owner, repository] = nameWithOwner.split('/');
    if (!owner || !repository) {
        throw new Error(`Invalid repository name: ${nameWithOwner}`);
    }
    return {owner, repository};
}

class ProjectAutomation {
    constructor({github, core, config}) {
        this.github = github;
        this.core = core;
        this.config = config;
        this.project = null;
        this.records = [];
    }

    async initialize() {
        const result = await this.github.graphql(
            `query($owner: String!, $number: Int!) {
                user(login: $owner) {
                    projectV2(number: $number) {
                        id
                        title
                        fields(first: 50) {
                            nodes {
                                ... on ProjectV2SingleSelectField {
                                    id
                                    name
                                    options { id name }
                                }
                            }
                        }
                    }
                }
            }`,
            {owner: this.config.projectOwner, number: this.config.projectNumber},
        );

        const project = result.user?.projectV2;
        if (!project) {
            throw new Error(`Project ${this.config.projectOwner}/${this.config.projectNumber} was not found.`);
        }
        const statusField = project.fields.nodes.find((field) => field?.name === 'Status');
        if (!statusField) {
            throw new Error(`Project ${project.title} has no Status field.`);
        }

        const statusOptions = new Map(statusField.options.map((option) => [option.name, option.id]));
        for (const status of Object.values(STATUS)) {
            if (!statusOptions.has(status)) {
                throw new Error(`Project ${project.title} is missing the '${status}' status option.`);
            }
        }

        this.project = {
            id: project.id,
            title: project.title,
            statusFieldId: statusField.id,
            statusOptions,
        };
    }

    async ensureProjectItem(contentId) {
        if (this.config.dryRun) {
            return null;
        }
        const result = await this.github.graphql(
            `mutation($project: ID!, $content: ID!) {
                addProjectV2ItemById(input: {projectId: $project, contentId: $content}) {
                    item { id }
                }
            }`,
            {project: this.project.id, content: contentId},
        );
        return result.addProjectV2ItemById.item.id;
    }

    async setStatus(contentId, displayName, status, reason) {
        const itemId = await this.ensureProjectItem(contentId);
        if (!this.config.dryRun) {
            await this.github.graphql(
                `mutation($project: ID!, $item: ID!, $field: ID!, $option: String!) {
                    updateProjectV2ItemFieldValue(input: {
                        projectId: $project
                        itemId: $item
                        fieldId: $field
                        value: {singleSelectOptionId: $option}
                    }) {
                        projectV2Item { id }
                    }
                }`,
                {
                    project: this.project.id,
                    item: itemId,
                    field: this.project.statusFieldId,
                    option: this.project.statusOptions.get(status),
                },
            );
        }
        this.records.push({item: displayName, status, reason});
        this.core.info(`${displayName} -> ${status}: ${reason}`);
    }

    async getIssue(owner, repository, number) {
        const result = await this.github.graphql(
            `query($owner: String!, $repository: String!, $number: Int!) {
                repository(owner: $owner, name: $repository) {
                    issue(number: $number) {
                        id
                        number
                        state
                        milestone { number title }
                        labels(first: 100) { nodes { name } }
                        assignees(first: 100) { nodes { login } }
                        closedByPullRequestsReferences(first: 50) {
                            nodes { number state isDraft reviewDecision }
                        }
                    }
                }
            }`,
            {owner, repository, number},
        );
        const issue = result.repository?.issue;
        if (!issue) {
            throw new Error(`Issue ${owner}/${repository}#${number} was not found.`);
        }
        return {
            id: issue.id,
            number: issue.number,
            state: issue.state,
            milestone: issue.milestone?.title ?? null,
            labels: issue.labels.nodes.map((label) => label.name),
            assignees: issue.assignees.nodes.map((assignee) => assignee.login),
            linkedPullRequests: issue.closedByPullRequestsReferences.nodes,
        };
    }

    async getPullRequest(owner, repository, number) {
        const result = await this.github.graphql(
            `query($owner: String!, $repository: String!, $number: Int!) {
                repository(owner: $owner, name: $repository) {
                    pullRequest(number: $number) {
                        id
                        number
                        state
                        isDraft
                        reviewDecision
                        author { login }
                        milestone { number title }
                        labels(first: 100) { nodes { name } }
                        assignees(first: 100) { nodes { login } }
                        closingIssuesReferences(first: 50) {
                            nodes {
                                id
                                number
                                state
                                milestone { number title }
                                labels(first: 100) { nodes { name } }
                                assignees(first: 100) { nodes { login } }
                            }
                        }
                    }
                }
            }`,
            {owner, repository, number},
        );
        const pullRequest = result.repository?.pullRequest;
        if (!pullRequest) {
            throw new Error(`Pull request ${owner}/${repository}#${number} was not found.`);
        }
        return {
            id: pullRequest.id,
            number: pullRequest.number,
            state: pullRequest.state,
            isDraft: pullRequest.isDraft,
            reviewDecision: pullRequest.reviewDecision,
            author: pullRequest.author?.login ?? null,
            milestone: pullRequest.milestone,
            labels: pullRequest.labels.nodes.map((label) => label.name),
            assignees: pullRequest.assignees.nodes.map((assignee) => assignee.login),
            linkedIssues: pullRequest.closingIssuesReferences.nodes.map((issue) => ({
                id: issue.id,
                number: issue.number,
                state: issue.state,
                milestone: issue.milestone,
                labels: issue.labels.nodes.map((label) => label.name),
                assignees: issue.assignees.nodes.map((assignee) => assignee.login),
            })),
        };
    }

    async getIssueDependencies(owner, repository, number, relationship) {
        const response = await this.github.request(
            `GET /repos/{owner}/{repo}/issues/{issue_number}/dependencies/${relationship}`,
            {
                owner,
                repo: repository,
                issue_number: number,
                per_page: 100,
                headers: {'X-GitHub-Api-Version': '2026-03-10'},
            },
        );
        return response.data;
    }

    async synchronizePullRequestMetadata(owner, repository, pullRequest) {
        const inherited = mergeLinkedIssueMetadata(pullRequest.linkedIssues, pullRequest.author);
        const existingLabels = new Set(pullRequest.labels);
        const labelsToAdd = inherited.labels.filter((label) => !existingLabels.has(label));

        if (labelsToAdd.length > 0 && !this.config.dryRun) {
            await this.github.rest.issues.addLabels({
                owner,
                repo: repository,
                issue_number: pullRequest.number,
                labels: labelsToAdd,
            });
        }

        const existingAssignees = new Set(pullRequest.assignees);
        const assigneesToAdd = inherited.assignees.filter((assignee) => !existingAssignees.has(assignee));
        if (assigneesToAdd.length > 0 && !this.config.dryRun) {
            await this.github.rest.issues.addAssignees({
                owner,
                repo: repository,
                issue_number: pullRequest.number,
                assignees: assigneesToAdd,
            });
        }

        if (inherited.milestone !== null && pullRequest.milestone === null && !this.config.dryRun) {
            await this.github.rest.issues.update({
                owner,
                repo: repository,
                issue_number: pullRequest.number,
                milestone: inherited.milestone.number,
            });
        }

        if (inherited.milestoneConflict.length > 0) {
            this.core.warning(
                `PR #${pullRequest.number} links issues with conflicting milestones: ${inherited.milestoneConflict.join(', ')}.`,
            );
        } else if (
            inherited.milestone !== null &&
            pullRequest.milestone !== null &&
            inherited.milestone.number !== pullRequest.milestone.number
        ) {
            this.core.warning(
                `PR #${pullRequest.number} keeps its existing milestone '${pullRequest.milestone.title}' instead of replacing it with '${inherited.milestone.title}'.`,
            );
        }
    }

    async reconcileIssue(owner, repository, number, {synchronizePullRequests = true} = {}) {
        const issue = await this.getIssue(owner, repository, number);
        const blockers = await this.getIssueDependencies(owner, repository, number, 'blocked_by');
        const openBlockers = blockers.filter((blocker) => blocker.state === 'open');
        const status = deriveIssueStatus({...issue, openBlockerCount: openBlockers.length}, this.config.activeMilestone);

        let reason = 'not yet ready for the active milestone';
        if (status === STATUS.DONE) reason = 'issue is closed';
        else if (status === STATUS.BLOCKED) reason = `${openBlockers.length} blocking issue(s) remain open`;
        else if (status === STATUS.REVIEW) reason = 'a linked pull request is ready for review';
        else if (status === STATUS.IN_PROGRESS) reason = 'the issue is assigned or has an active draft pull request';
        else if (status === STATUS.READY) reason = 'active milestone metadata is complete and no blockers remain';

        await this.setStatus(issue.id, `Issue #${number}`, status, reason);

        if (synchronizePullRequests) {
            for (const pullRequest of issue.linkedPullRequests.filter((candidate) => candidate.state === 'OPEN')) {
                await this.reconcilePullRequest(owner, repository, pullRequest.number, {synchronizeIssues: false});
            }
        }
    }

    async reconcilePullRequest(owner, repository, number, {synchronizeIssues = true} = {}) {
        const pullRequest = await this.getPullRequest(owner, repository, number);
        await this.synchronizePullRequestMetadata(owner, repository, pullRequest);

        const status = derivePullRequestStatus(pullRequest);
        let reason = 'pull request is ready for review';
        if (status === STATUS.DONE) reason = 'pull request is closed or merged';
        else if (status === STATUS.IN_PROGRESS) reason = 'pull request is draft or has requested changes';
        await this.setStatus(pullRequest.id, `PR #${number}`, status, reason);

        if (synchronizeIssues) {
            for (const issue of pullRequest.linkedIssues) {
                await this.reconcileIssue(owner, repository, issue.number, {synchronizePullRequests: false});
            }
        }
    }

    async reconcileDependents(owner, repository, number) {
        const dependents = await this.getIssueDependencies(owner, repository, number, 'blocking');
        for (const dependent of dependents.filter((issue) => issue.state === 'open')) {
            await this.reconcileIssue(owner, repository, dependent.number);
        }
    }

    async listProjectItems() {
        const items = [];
        let cursor = null;
        do {
            const result = await this.github.graphql(
                `query($project: ID!, $cursor: String) {
                    node(id: $project) {
                        ... on ProjectV2 {
                            items(first: 100, after: $cursor) {
                                nodes {
                                    content {
                                        __typename
                                        ... on Issue { number repository { nameWithOwner } }
                                        ... on PullRequest { number repository { nameWithOwner } }
                                    }
                                }
                                pageInfo { hasNextPage endCursor }
                            }
                        }
                    }
                }`,
                {project: this.project.id, cursor},
            );
            const page = result.node.items;
            items.push(...page.nodes.map((node) => node.content).filter(Boolean));
            cursor = page.pageInfo.hasNextPage ? page.pageInfo.endCursor : null;
        } while (cursor !== null);
        return items;
    }

    async reconcileAll() {
        const items = await this.listProjectItems();
        for (const item of items) {
            if (item.repository.nameWithOwner !== this.config.repository) {
                continue;
            }
            const {owner, repository} = splitRepository(item.repository.nameWithOwner);
            if (item.__typename === 'Issue') {
                await this.reconcileIssue(owner, repository, item.number, {synchronizePullRequests: false});
            } else if (item.__typename === 'PullRequest') {
                await this.reconcilePullRequest(owner, repository, item.number, {synchronizeIssues: false});
            }
        }
    }

    async writeSummary() {
        const mode = this.config.dryRun ? 'Dry run' : 'Applied';
        const lines = [`## Project automation`, '', `${mode} for **${this.project.title}**.`, ''];
        if (this.records.length === 0) {
            lines.push('No project items required reconciliation.');
        } else {
            lines.push('| Item | Status | Reason |', '| --- | --- | --- |');
            for (const record of this.records) {
                lines.push(`| ${record.item} | ${record.status} | ${record.reason.replaceAll('|', '\\|')} |`);
            }
        }
        await this.core.summary.addRaw(`${lines.join('\n')}\n`).write();
    }
}

function readConfig(context, environment) {
    const projectNumber = Number.parseInt(environment.PROJECT_NUMBER ?? '', 10);
    const config = {
        projectOwner: environment.PROJECT_OWNER ?? '',
        projectNumber,
        activeMilestone: environment.ACTIVE_MILESTONE ?? '',
        repository: context.payload.repository?.full_name ?? environment.GITHUB_REPOSITORY ?? '',
        dryRun: String(environment.DRY_RUN ?? '').toLowerCase() === 'true',
    };
    if (!config.projectOwner || !Number.isInteger(config.projectNumber) || !config.activeMilestone || !config.repository) {
        throw new Error('PROJECT_OWNER, PROJECT_NUMBER, ACTIVE_MILESTONE, and GITHUB_REPOSITORY must be configured.');
    }
    return config;
}

async function run({github, context, core, environment = process.env}) {
    const config = readConfig(context, environment);
    const automation = new ProjectAutomation({github, core, config});
    await automation.initialize();

    const {owner, repository} = splitRepository(config.repository);
    if (context.eventName === 'issues') {
        await automation.reconcileIssue(owner, repository, context.payload.issue.number);
        await automation.reconcileDependents(owner, repository, context.payload.issue.number);
    } else if (context.eventName === 'pull_request_target') {
        await automation.reconcilePullRequest(owner, repository, context.payload.pull_request.number);
    } else if (context.eventName === 'workflow_dispatch' && context.payload.inputs?.kind !== 'all') {
        const number = Number.parseInt(context.payload.inputs?.number ?? '', 10);
        if (!Number.isInteger(number)) {
            throw new Error('A numeric issue or pull request number is required for focused reconciliation.');
        }
        if (context.payload.inputs.kind === 'issue') {
            await automation.reconcileIssue(owner, repository, number);
        } else {
            await automation.reconcilePullRequest(owner, repository, number);
        }
    } else {
        await automation.reconcileAll();
    }

    await automation.writeSummary();
}

module.exports = {
    STATUS,
    ProjectAutomation,
    deriveIssueStatus,
    derivePullRequestStatus,
    hasRequiredLabels,
    mergeLinkedIssueMetadata,
    readConfig,
    run,
    splitRepository,
};
