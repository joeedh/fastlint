import fs from "node:fs";
import path from "node:path";
import type { CommandModule } from "yargs";
import { fail, info, step } from "./lib/log.ts";
import { repoRoot, vendorDir } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";

interface External {
  repo: string;
  /** Pinned commit, so a fresh clone builds the same source as everyone else. */
  commit: string;
  describe: string;
}

/**
 * Externals we clone rather than submodule, because they are third-party
 * sources we only consume. Pinned by commit and checked out under vendor/.
 */
const externals: Record<string, External> = {
  dtl: {
    repo    : "https://github.com/joeedh/dtl.git",
    commit  : "a55c9c639989b25ae53d941db24c7fa7d84b51e5",
    describe: "diff template library, used for snapshot diffs in the test framework",
  },
};

async function fetchExternal(name: string): Promise<void> {
  const external = externals[name];
  if (!external) {
    fail(`unknown external ${name}; known: ${Object.keys(externals).join(", ")}`);
  }
  const dir = path.join(vendorDir, name);
  if (!fs.existsSync(path.join(dir, ".git"))) {
    step(`clone ${external.repo} -> vendor/${name}`);
    fs.mkdirSync(vendorDir, { recursive: true });
    await run("git", ["clone", "--quiet", external.repo, dir], { cwd: repoRoot });
  }
  step(`checkout ${name} @ ${external.commit.slice(0, 10)}`);
  await run("git", ["fetch", "--quiet", "origin", external.commit], {
    cwd         : dir,
    allowFailure: true,
  });
  await run("git", ["checkout", "--quiet", "--detach", external.commit], { cwd: dir });
}

export const command: CommandModule<object, { target?: string; name?: string }> = {
  command : "deps [target] [name]",
  describe: "update submodules; `deps fetch <name>` clones a pinned external",
  builder: (yargs) =>
    yargs
      .positional("target", {
        type    : "string",
        describe: "omit to update submodules, or `fetch`",
      })
      .positional("name", { type: "string", describe: "external to fetch" }),
  handler: async (argv) => {
    if (argv.target === "fetch") {
      if (!argv.name)
        fail(`deps fetch needs a name: ${Object.keys(externals).join(", ")}`);
      await fetchExternal(argv.name);
      return;
    }
    if (argv.target) fail(`unknown deps subcommand ${argv.target}`);

    step("submodule update --init --recursive");
    await run("git", ["submodule", "update", "--init", "--recursive"], { cwd: repoRoot });
    for (const name of Object.keys(externals)) {
      await fetchExternal(name);
    }
    info("dependencies up to date");
  },
};
