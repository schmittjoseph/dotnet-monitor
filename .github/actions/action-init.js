export async function run(main, extraDependencies = [])
{
    const util = require('util');
    const jsExec = util.promisify(require("child_process").exec);

    const githubPackage = "@actions/github";
    const corePackage = "@actions/core";

    const { stdout, stderr } = await jsExec(`npm install ${corePackage} ${githubPackage} ${extraDependencies.join(' ')}`);
    console.log("npm-install stderr:\n\n" + stderr);
    console.log("npm-install stdout:\n\n" + stdout);
    console.log("Finished installing npm dependencies");

    const github = require(githubPackage);
    const core = require(corePackage);

    const token = core.getInput('auth_token', {required: true});
    const debug = core.getInput('debug');

    const octokit = github.getOctokit(token, {
        log: (debug === 'true') ? console : undefined
    });

    try {
        await main(core, github, octokit);
    } catch (error) {
        console.error(error);
        core.setFailed(error);
    }
}