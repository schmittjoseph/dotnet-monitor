const actionUtils = require('../action-utils.js');

class Suggestion {
    constructor(file, startingLine) {
        this.file = file;
        this.startingLine = startingLine;
        this.numberOfLinesToChange = -1;
        this.hasContext = false;
        this.body = [];
    }

    removeLine() {
        this.numberOfLinesToChange++;
    }

    addContext(context) {
        this.hasContext = true;
        this.numberOfLinesToChange++;
        this.body.push(context);
    }

    addLine(line) {
        this.body.push(line);
    }

    getCommentBody() {
        if (this.body.length === 0) {
            return `\`\`\`suggestion\n\`\`\``;
        } else {
            return `\`\`\`suggestion\n${this.body.join('\n')}\n\`\`\``;
        }
    }
}

async function run() {
    const [core, github] = await actionUtils.installAndRequirePackages("@actions/core", "@actions/github");

    const octokit = github.getOctokit(core.getInput("auth_token", { required: true }));
    const diffFile = core.getInput("diff_file", { required: true });
    const reporter = core.getInput("reporter", { required: true });

    const repoOwner = github.context.payload.repository.owner.login;
    const repoName = github.context.payload.repository.name;

    const triggeringPr = github.context.payload.workflow_run.pull_requests[0];
    const prNumber = triggeringPr.number;
    const commitId = triggeringPr.head.sha;

    const formattedReporter = `**${reporter}**`;

    const maxSuggestionsInput = core.getInput("max_suggestions", { required: false });
    const runLocalCommand = core.getInput("run_local_command", { required: false });

    let maxSuggestions = undefined;
    if (maxSuggestionsInput) {
        maxSuggestions = parseInt(maxSuggestionsInput);
        if (runLocalCommand === undefined) {
            throw new Error("If a maximum number of suggestions is set, a run local command must also be provided.")
        }
    }

    try {
        const suggestions = await getAllSuggestions(diffFile);
        await submitSuggestions(octokit, prNumber, commitId, repoOwner, repoName, formattedReporter, maxSuggestions, runLocalCommand, suggestions);
    } catch (error) {
        core.setFailed(error);

        let messageBody = `${formattedReporter} Was unable to create all linter suggestions, for more details see https://github.com/${repoOwner}/${repoName}/actions/runs/${process.env.GITHUB_RUN_ID}`;
        if (runLocalCommand) {
            messageBody += `

To run the linter locally, please use: \`${runLocalCommand}\``;
        }

        await octokit.rest.issues.createComment({
            owner: repoOwner,
            repo: repoName,
            issue_number: prNumber,
            commit_id: commitId,
            body:messageBody});
    }
}

async function submitSuggestions(octokit, prNumber, commitId, owner, repo, reporter, maxSuggestions, runLocalCommand, suggestions) {
    if (suggestions.length === 0) {
        return;
    }

    if (maxSuggestions !== undefined && suggestions.length >= maxSuggestions) {
        await octokit.rest.issues.createComment({
            owner: owner,
            repo: repo,
            issue_number: prNumber,
            commit_id: commitId,
            body:`${reporter} is reporting too many linter changes (${suggestions.length}), please fix them locally and update this PR.

To fix them locally, please run: \`${runLocalCommand}\``});

        throw new Error(`Too many suggestions ${suggestions.length}/${maxSuggestions}`)
    }

    // Get all of the suggestions for the PR already made. We do this to avoid duplicate entries from being created and spamming the user.
    const existingComments = await octokit.paginate(octokit.rest.pulls.listReviewComments, {
        owner: owner,
        repo: repo,
        pull_number: prNumber,
    });

    let fileToComments = new Map();
    for (const comment of existingComments) {
        if (fileToComments.has(comment.path)) {
            fileToComments.get(comment.path).push(comment);
        } else {
            fileToComments.set(comment.path, [comment]);
        }
    }

    // Transform the suggestions into comments
    const comments = [];
    for (const suggestion of suggestions) {
        // https://docs.github.com/en/rest/pulls/comments?apiVersion=2022-11-28#create-a-review-comment-for-a-pull-request for comment payload format
        let comment = {
            path: suggestion.file,
            side: 'RIGHT',
            body: `${reporter}\n${suggestion.getCommentBody()}`
        };

        const numberOfLines = suggestion.numberOfLinesToChange;
        if (numberOfLines > 0) {
            comment.start_line = suggestion.startingLine;
            comment.line = suggestion.startingLine + numberOfLines;
            comment.start_side = 'RIGHT';
        } else {
            comment.line = suggestion.startingLine;
        }

        let foundExisting = false;
        if (fileToComments.has(comment.path)) {
            for (const existingComment of fileToComments.get(comment.path)) {
                if (existingComment.line === comment.line &&
                    existingComment.start_line === comment.start_line &&
                    existingComment.body === comment.body) {
                        foundExisting = true;
                        break;
                    }
            }
        }

        if (foundExisting) {
            console.log("Skipping duplicate suggestion: ");
            console.log(suggestion);
            continue;
        }

        comments.push(comment);
    }

    if (comments.length === 0) {
        return;
    }

    // Submit a review with the comments
    await octokit.rest.pulls.createReview({
        owner: owner,
        repo: repo,
        pull_number: prNumber,
        commit_id: commitId,
        event: 'COMMENT',
        body: '',
        comments: comments
    });
}

class HunkTransformer {
    #contextPrefix = " ";
    #delPrefix = "-";
    #addPrefix = "+";

    constructor(file, hunkLine) {
        const hunkRegex=/^@@ -(?<srcLine>\d+),?(?<srcLength>\d+)* \+(?<dstLine>\d+),?(?<dstLength>\d+)? @@/m

        const match = hunkLine.match(hunkRegex);
        this.startingLine = parseInt(match.groups.srcLine.trim());
        this.hunkLength = match.groups.srcLength === undefined ? 0 : parseInt(match.groups.srcLength.trim()) - 1;

        this.effectiveLine = this.startingLine;

        this.file = file;

        this.suggestionBufferMode = undefined;
        this.suggestionBuffer = undefined;
        this.suggestions = [];
    }

    #commitSuggestionBuffer = function() {
        if (this.suggestionBuffer !== undefined &&
            this.suggestionBufferMode !== this.#contextPrefix) {

            this.suggestions.push(this.suggestionBuffer);
            this.suggestionBuffer = undefined;
            this.suggestionBufferMode = undefined;
        }
    }

    #stageNewSuggestionBufferIfNeeded = function(mode) {
        if (this.suggestionBufferMode !== mode ||
            (this.suggestionBufferMode === this.#contextPrefix && mode === this.#contextPrefix)) {
            this.#commitSuggestionBuffer();
            this.suggestionBuffer = new Suggestion(this.file, this.effectiveLine);
            this.suggestionBufferMode = mode;
        }
    }

    processLine(line) {
        if (line.startsWith(this.#contextPrefix)) {
            if (this.suggestionBufferMode == this.#delPrefix ||
                this.suggestionBufferMode == this.#contextPrefix ||
                (this.suggestionBufferMode == this.#addPrefix && this.suggestionBuffer.hasContext)) {
                this.#stageNewSuggestionBufferIfNeeded(this.#contextPrefix);
            }

            this.suggestionBuffer.addContext(line.substring(this.#contextPrefix.length))
            this.effectiveLine++;
        } else if (line.startsWith(this.#delPrefix)) {
            this.#stageNewSuggestionBufferIfNeeded(this.#delPrefix);
            this.suggestionBuffer.removeLine();
            this.effectiveLine++;
        } else if (line.startsWith(this.#addPrefix)) {
            if (this.suggestionBufferMode == this.#contextPrefix) {
                // Take ownership of the current buffer.
                this.suggestionBufferMode = this.#addPrefix;
            }

            this.#stageNewSuggestionBufferIfNeeded(this.#addPrefix);
            this.suggestionBuffer.addLine(line.substring(this.#addPrefix.length));

        } else {
            // Hunk has finished.
            this.#commitSuggestionBuffer();
            return false;
        }

        return true;
    }
}

async function getAllSuggestions(diffFile) {
    let diffContents = await actionUtils.readFile(diffFile);

    let allSuggestions = [];
    let hunkTransformer = undefined;

    let srcFile = undefined;
    let dstFile = undefined;

    let inFile = false;

    const srcFilePrefix = "--- ";
    const dstFilePrefix = "+++ ";
    const hunkPrefix = "@@ ";

    const diffLines = diffContents.split(/\r?\n/);
    for (const line of diffLines)
    {
        if (hunkTransformer !== undefined) {
            if (hunkTransformer.processLine(line)) {
                continue;
            } else {
                // Hunk is done.
                allSuggestions = allSuggestions.concat(hunkTransformer.suggestions);
            }
        }

        if (line.startsWith(srcFilePrefix)) {
            srcFile = line.substring(srcFilePrefix.length).trim();
            inFile = false;
        } else if (line.startsWith(dstFilePrefix)) {
            dstFile = line.substring(dstFilePrefix.length).trim();
            if (dstFile !== srcFile) {
                throw new Error(`The source and destination files for the hunk are different! The diff must not contain prefixes or file renames. (src: ${srcFile} dst:${dstFile}`)
            }
            inFile = true;
        } else if (line.startsWith(hunkPrefix)) {
            if (!inFile) {
                throw new Error("Invalid diff file.")
            }

            hunkTransformer = new HunkTransformer(srcFile, line);
        }
    }

    return allSuggestions;
}

run();
