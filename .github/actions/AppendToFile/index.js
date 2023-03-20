const fs = require('fs');
const path = require('path')

/* Run the action */
require('../action-init.js').run(main);

async function main(core, github, octokit) {
    const textToSearch = core.getInput('textToSearch', { required: true });
    const textToAdd = core.getInput('textToAdd', { required: true });
    const paths = core.getInput('paths', {required: false});

    const insertFileNameParameter = "{insertFileName}";

    if (paths === null || paths.trim() === "")
    {
        return;
    }

    console.log("Paths: " + paths);

    for (const currPath of paths.split(',')) {
        fs.readFile(currPath, (err, content) => {
            if (err)
            {
                console.log(err);
            }

            if (content && !content.includes(textToSearch))
            {
                var updatedTextToAdd = textToAdd;
                if (textToAdd.includes(insertFileNameParameter))
                {
                    const parsedPath = path.parse(currPath);
                    const encodedURIWithoutExtension = encodeURIComponent(path.join(parsedPath.dir, parsedPath.name))
                    updatedTextToAdd = textToAdd.replace(insertFileNameParameter, encodedURIWithoutExtension);
                }

                var contentStr = updatedTextToAdd + "\n\n" + content.toString();

                fs.writeFile(currPath, contentStr, (err) => {});
            }
        });
    }
}