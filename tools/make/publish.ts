// The last step of a release, kept apart from `release` because it is the only
// irreversible one: an unpublished version can be cut again, a published one
// cannot. It sends the tarball `release` built and checked, rather than packing
// a fresh one, so what reaches the registry is what the install test ran.

import fs from "node:fs";
import path from "node:path";
import type { CommandModule } from "yargs";

import { color, fail, info, step, warn } from "./lib/log.ts";
import { npm, npmWhoami, publishBlocker, publishedVersions } from "./lib/npm.ts";
import { repoRoot } from "./lib/paths.ts";
import { capture } from "./lib/spawn.ts";
import { readVersions } from "./lib/version.ts";
import { releaseDir, tarballName } from "./pack.ts";

interface Args {
  tag: string;
  otp?: string;
  dryRun: boolean;
}

export const command: CommandModule<object, Args> = {
  command : "publish",
  describe: "send the tarball `release` built to the npm registry",
  builder: (yargs) =>
    yargs
      .option("tag", {
        type    : "string",
        default : "latest",
        describe: "the dist-tag to publish under",
      })
      .option("otp", {
        type    : "string",
        describe: "one-time password, for an account with 2FA on publishes",
      })
      .option("dry-run", {
        type    : "boolean",
        default : false,
        describe: "let npm report what it would send without sending it",
      })
      .example("$0 publish --dry-run", "list what would go to the registry")
      .example("$0 publish --tag next", "publish under the next dist-tag")
      .epilogue(
        `Run ${color.cyan("node make.ts release <bump>")} first. See ` +
          `${color.cyan("docs/embedding.md")} "Releasing".`
      ) as never,

  handler: async (argv) => {
    const { manifest: version, name } = readVersions();
    const tarball = path.join(releaseDir, tarballName(name, version));
    if (!fs.existsSync(tarball)) {
      fail(
        `no ${path.relative(repoRoot, tarball)}; run \`node make.ts release <bump>\` first`
      );
    }

    const stray = fs
      .readdirSync(releaseDir)
      .filter((name) => name.endsWith(".tgz") && name !== path.basename(tarball));
    if (stray.length > 0) {
      warn(`ignoring an older tarball in build/release: ${stray.join(", ")}`);
    }

    const tagged = capture("git", ["tag", "--list", `v${version}`], { cwd: repoRoot });
    if (!tagged?.trim()) {
      warn(`there is no v${version} tag; \`release\` normally writes one`);
    }

    const published = publishedVersions(name);
    if (published?.includes(version)) fail(`${name} ${version} is already published`);

    const who = npmWhoami();
    if (!who) {
      fail("not logged in to npm; run `npm login` in a terminal, then this again");
    }
    const blocker = publishBlocker(name, who);
    if (blocker) fail(blocker);
    info(`publishing ${name} ${version} as ${who} under the '${argv.tag}' tag`);

    step("npm publish");
    const result = await npm(
      [
        "publish",
        tarball,
        "--access",
        "public",
        "--tag",
        argv.tag,
        ...(argv.otp ? ["--otp", argv.otp] : []),
        ...(argv.dryRun ? ["--dry-run"] : []),
      ],
      { cwd: repoRoot, allowFailure: true }
    );
    if (result.code !== 0) fail(`npm publish exited ${result.code}`);

    if (argv.dryRun) {
      info(color.green(`dry run: nothing was sent`));
      return;
    }
    info(color.green(`published ${name} ${version}`));
    info(`check it with \`npm view ${name}@${version}\``);
  },
};
