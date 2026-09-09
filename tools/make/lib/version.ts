// The one version the project ships under. It is written in two places, since
// the native binary cannot read package.json and npm cannot read a C++ source,
// and a release writes both together so `fastlint --version` and the installed
// package never disagree.

import fs from "node:fs";
import path from "node:path";

import { repoRoot } from "./paths.ts";

const manifestPath = path.join(repoRoot, "package.json");
const nativePath = path.join(repoRoot, "source", "fastlint", "version.cc");

/** `return "1.2.3";` in version.cc, which is the whole of `fastlint::version`. */
const nativeVersion = /return\s+"(\d+\.\d+\.\d+)";/;

export interface Versions {
  /** The package name, which the release tasks need for every registry query. */
  name: string;
  manifest: string;
  native: string;
}

export function readVersions(): Versions {
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8")) as {
    name?: string;
    version?: string;
  };
  const native = nativeVersion.exec(fs.readFileSync(nativePath, "utf8"));
  if (!manifest.name) throw new Error("package.json has no name");
  if (!manifest.version) throw new Error("package.json has no version");
  if (!native) throw new Error(`no version literal in ${nativePath}`);
  return { name: manifest.name, manifest: manifest.version, native: native[1]! };
}

/** Both files as they stand, so a failed release can put them back. */
export function versionFiles(): { file: string; text: string }[] {
  return [manifestPath, nativePath].map((file) => ({
    file,
    text: fs.readFileSync(file, "utf8"),
  }));
}

export function restore(saved: readonly { file: string; text: string }[]): void {
  for (const { file, text } of saved) fs.writeFileSync(file, text);
}

/** Writes `next` to both files, leaving the rest of each byte for byte. */
export function writeVersion(next: string): void {
  const manifest = fs.readFileSync(manifestPath, "utf8");
  const bumped = manifest.replace(/("version"\s*:\s*)"\d+\.\d+\.\d+"/, `$1"${next}"`);
  if (bumped === manifest) throw new Error("package.json version was not rewritten");
  fs.writeFileSync(manifestPath, bumped);

  const native = fs.readFileSync(nativePath, "utf8");
  const rewritten = native.replace(nativeVersion, `return "${next}";`);
  if (rewritten === native) throw new Error(`${nativePath} version was not rewritten`);
  fs.writeFileSync(nativePath, rewritten);
}

export function parseVersion(text: string): [number, number, number] {
  const parts = text.split(".");
  if (parts.length !== 3 || !parts.every((part) => /^\d+$/.test(part))) {
    throw new Error(`not a version X.Y.Z: ${text}`);
  }
  return parts.map(Number) as [number, number, number];
}

/** The version a bump lands on. An explicit X.Y.Z is taken as given, and has to
 * be greater than the current one, since npm refuses a republish either way. */
export function nextVersion(kind: string, current: string): string {
  const [major, minor, patch] = parseVersion(current);
  if (kind === "major") return `${major + 1}.0.0`;
  if (kind === "minor") return `${major}.${minor + 1}.0`;
  if (kind === "patch") return `${major}.${minor}.${patch + 1}`;
  const asked = parseVersion(kind);
  const ordered = (a: readonly number[], b: readonly number[]): boolean =>
    a[0]! !== b[0]! ? a[0]! > b[0]! : a[1]! !== b[1]! ? a[1]! > b[1]! : a[2]! > b[2]!;
  if (!ordered(asked, [major, minor, patch])) {
    throw new Error(`${kind} is not greater than the current version ${current}`);
  }
  return kind;
}
