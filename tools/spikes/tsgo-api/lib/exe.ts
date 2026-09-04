import { createRequire } from "node:module";
import { pathToFileURL } from "node:url";
import path from "node:path";

const require = createRequire(import.meta.url);

/** Absolute path to the native `tsc` executable shipped with the local typescript package. */
export async function resolveTscExe(): Promise<string> {
  const pkg = require.resolve("typescript/package.json");
  const helper = path.join(path.dirname(pkg), "lib", "getExePath.js");
  const mod = (await import(pathToFileURL(helper).href)) as { default: () => string };
  return mod.default();
}
