// Cutting a release. Everything that can fail is done before anything is
// pushed, so a run that dies partway never leaves a tag on the remote with no
// release behind it, or a published version nobody can build.
//
// The last step is deliberately not `npm publish`: a release is reversible up to
// that point (a tag can be deleted, a GitHub release edited), and a published
// version is not. `node make.ts publish` sends the tarball this built.

import path from "node:path";
import type { CommandModule } from "yargs";

import { color, fail, info, step, warn } from "./lib/log.ts";
import { npmWhoami, publishBlocker, publishedVersions } from "./lib/npm.ts";
import { repoRoot } from "./lib/paths.ts";
import { capture, run } from "./lib/spawn.ts";
import {
  nextVersion,
  readVersions,
  restore,
  versionFiles,
  writeVersion,
} from "./lib/version.ts";
import { buildPackage, installTest, packTarball } from "./pack.ts";

interface Args {
  bump: string;
  check: boolean;
  installTest: boolean;
  wasm: boolean;
  dryRun: boolean;
}

const git = (...args: string[]): string | undefined =>
  capture("git", args, { cwd: repoRoot });

/** Refuses the release now rather than partway through it. */
function preflight(dryRun: boolean): void {
  step("preflight");

  const dirty = git("status", "--porcelain");
  if (dirty === undefined) fail("git status failed");
  if (dirty.trim().length > 0) {
    fail("the working tree has uncommitted changes; commit or stash them first");
  }

  const branch = git("rev-parse", "--abbrev-ref", "HEAD")?.trim();
  if (branch !== "master") {
    warn(`releasing from '${branch}' rather than master`);
  }

  const { manifest, native } = readVersions();
  if (manifest !== native) {
    fail(`package.json says ${manifest} and source/fastlint/version.cc says ${native}`);
  }

  if (dryRun) return;
  if (capture("gh", ["auth", "status"]) === undefined) {
    fail("not authenticated with gh; run `gh auth login` first");
  }
  info(`on ${branch} at ${manifest}, working tree clean, gh authenticated`);
}

/** Fails when `version` is already taken, since neither a git tag nor an npm
 * version can be reused. A registry the run cannot reach is only reported:
 * being offline is not a reason to refuse a release, since publishing is a
 * separate command that checks again. */
function checkUnreleased(name: string, version: string): void {
  const tag = `v${version}`;
  if (git("tag", "--list", tag)?.trim()) fail(`the tag ${tag} already exists`);
  if (git("ls-remote", "--tags", "origin", tag)?.trim()) {
    fail(`the tag ${tag} is already on origin`);
  }
  const published = publishedVersions(name);
  if (published === undefined) {
    warn("could not reach the registry; not checking whether the version is published");
    return;
  }
  if (published.includes(version)) fail(`${name} ${version} is already published`);

  // Reported rather than refused: the tag and the GitHub release are worth
  // cutting even where npm will not take the name, and `publish` refuses.
  const who = npmWhoami();
  const blocker = who ? publishBlocker(name, who) : undefined;
  if (blocker) warn(`${blocker}. \`node make.ts publish\` will refuse this release`);
}

export const command: CommandModule<object, Args> = {
  command : "release <bump>",
  describe: "bump the version, build and check the package, tag and push",
  builder: (yargs) =>
    yargs
      .positional("bump", {
        type    : "string",
        describe: "major, minor, patch, or an explicit X.Y.Z",
      })
      .option("check", {
        type    : "boolean",
        default : true,
        describe: "run `make.ts check` first (--no-check skips it)",
      })
      .option("install-test", {
        type    : "boolean",
        default : true,
        describe: "install the tarball in a throwaway project and lint through it",
      })
      .option("wasm", {
        type    : "boolean",
        default : true,
        describe: "build the release WASM engine (--no-wasm reuses the built one)",
      })
      .option("dry-run", {
        type    : "boolean",
        default : false,
        describe: "do everything up to the tarball, then stop before git and gh",
      })
      .example("$0 release patch", "cut 0.1.0 to 0.1.1 and push the tag")
      .example("$0 release 1.0.0 --dry-run", "build and check what 1.0.0 would ship")
      .epilogue(
        `Then ${color.cyan("node make.ts publish")} sends it. See ` +
          `${color.cyan("docs/embedding.md")} "Releasing".`
      ) as never,

  handler: async (argv) => {
    preflight(argv.dryRun);
    const { name, manifest: current } = readVersions();
    let version: string;
    try {
      version = nextVersion(argv.bump, current);
    } catch (error) {
      fail(error instanceof Error ? error.message : String(error));
    }
    checkUnreleased(name, version);
    info(`${current} -> ${color.bold(version)}`);

    if (argv.check) {
      step("check");
      const checked = await run(process.execPath, ["make.ts", "check"], {
        cwd         : repoRoot,
        allowFailure: true,
      });
      if (checked.code !== 0) fail("check failed; nothing was changed");
    }

    // The version goes in before the build so the tarball, the tag and what
    // `lintrix --version` prints are all the same number. Both files go back
    // as they were if anything after this fails.
    const saved = versionFiles();
    const undo = (message: string): never => {
      restore(saved);
      return fail(`${message}; the version files were put back`);
    };
    writeVersion(version);
    step(`wrote ${version} to package.json and source/fastlint/version.cc`);

    let tarball: string;
    try {
      const released = await buildPackage({ wasm: argv.wasm, smoke: true });
      if (!released) undo("the package bundled the debug WASM engine; drop --no-wasm");
      tarball = await packTarball();
      if (argv.installTest) await installTest(tarball);
    } catch (error) {
      return undo(error instanceof Error ? error.message : String(error));
    }

    if (argv.dryRun) {
      restore(saved);
      info(
        color.green(
          `dry run: ${path.relative(repoRoot, tarball)} is what ${version} would ship`
        )
      );
      info("the version files were put back; nothing was committed or pushed");
      return;
    }

    step("commit, tag and push");
    const added = await run(
      "git",
      ["add", "package.json", "source/fastlint/version.cc"],
      {
        cwd         : repoRoot,
        allowFailure: true,
      }
    );
    if (added.code !== 0) undo("git add failed");
    const committed = await run("git", ["commit", "-m", `Release v${version}`], {
      cwd         : repoRoot,
      allowFailure: true,
    });
    if (committed.code !== 0) undo("git commit failed");

    // Past the commit nothing is unwound here. Undoing a tag or a push takes a
    // git command the reader should choose, rather than one this task guesses at.
    await run("git", ["tag", `v${version}`], { cwd: repoRoot });
    await run("git", ["push", "origin", "HEAD", "--follow-tags"], { cwd: repoRoot });

    step("gh release create");
    await run(
      "gh",
      [
        "release",
        "create",
        `v${version}`,
        tarball,
        "--title",
        `v${version}`,
        "--generate-notes",
      ],
      { cwd: repoRoot }
    );

    info(color.green(`released v${version}`));
    info(
      `next: ${color.cyan("node make.ts publish")} sends ${path.basename(tarball)} to npm`
    );
  },
};
