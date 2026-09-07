import fs from "node:fs";
import path from "node:path";
import type { CommandModule } from "yargs";
import { fail, info, step } from "./lib/log.ts";
import { repoRoot, vendorDir } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";
import { extractZip } from "./lib/zip.ts";
import crypto from "node:crypto";

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

interface Archive {
  url: string;
  /** SHA3-256 of the zip as published beside the download. */
  sha3: string;
  describe: string;
  /** A file whose presence means the archive is already extracted. */
  marker: string;
}

/** Release archives we download rather than clone, pinned by checksum. */
const archives: Record<string, Archive> = {
  sqlite: {
    url     : "https://sqlite.org/2026/sqlite-amalgamation-3530400.zip",
    sha3    : "628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e",
    describe: "sqlite 3.53.4 amalgamation, the type cache store",
    marker  : "sqlite3.c",
  },
};

async function fetchArchive(name: string, force: boolean): Promise<void> {
  const archive = archives[name]!;
  const dir = path.join(vendorDir, name);
  const stamp = path.join(dir, ".sha3");
  if (!force && fs.existsSync(path.join(dir, archive.marker)) && fs.existsSync(stamp)) {
    if (fs.readFileSync(stamp, "utf8").trim() === archive.sha3) {
      info(`vendor/${name} is current`);
      return;
    }
  }
  step(`download ${archive.url}`);
  const response = await fetch(archive.url);
  if (!response.ok) fail(`download failed: ${response.status} ${response.statusText}`);
  const bytes = Buffer.from(await response.arrayBuffer());
  const digest = crypto.createHash("sha3-256").update(bytes).digest("hex");
  if (digest !== archive.sha3) {
    fail(`checksum mismatch for ${name}: expected ${archive.sha3}, got ${digest}`);
  }
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const written = extractZip(bytes, dir);
  fs.writeFileSync(stamp, archive.sha3 + "\n");
  info(`extracted ${written.length} files to vendor/${name}`);
}

async function fetchExternal(name: string): Promise<void> {
  if (archives[name]) {
    await fetchArchive(name, false);
    return;
  }
  const external = externals[name];
  if (!external) {
    fail(
      `unknown external ${name}; known: ${[...Object.keys(externals), ...Object.keys(archives)].join(", ")}`
    );
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

export const command: CommandModule<
  object,
  { target?: string; name?: string; force: boolean }
> = {
  command : "deps [target] [name]",
  describe:
    "update submodules and externals; `deps fetch <name>` clones or downloads one",
  builder: (yargs) =>
    yargs
      .positional("target", {
        type    : "string",
        describe: "omit to update submodules, or `fetch`",
      })
      .positional("name", { type: "string", describe: "external to fetch" })
      .option("force", {
        type    : "boolean",
        default : false,
        describe: "refetch an archive even when its checksum stamp is current",
      }),
  handler: async (argv) => {
    if (argv.target === "fetch") {
      const known = [...Object.keys(externals), ...Object.keys(archives)].join(", ");
      if (!argv.name) fail(`deps fetch needs a name: ${known}`);
      if (archives[argv.name]) await fetchArchive(argv.name, argv.force);
      else await fetchExternal(argv.name);
      return;
    }
    if (argv.target) fail(`unknown deps subcommand ${argv.target}`);

    step("submodule update --init --recursive");
    await run("git", ["submodule", "update", "--init", "--recursive"], { cwd: repoRoot });
    for (const name of [...Object.keys(externals), ...Object.keys(archives)]) {
      await fetchExternal(name);
    }
    info("dependencies up to date");
  },
};
