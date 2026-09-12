// Bundles the client and the server into out/, one file each. `node esbuild.ts`
// builds once; `--watch` rebuilds on change while a debug session runs.
//
// Both bundles are CommonJS, the format the extension host loads. The plugin
// sources read `import.meta.url` to find the WASM module and the worker beside
// them; in a CommonJS bundle it would be empty, so it is defined to the bundle's
// own URL, which makes `findWasmModule` resolve to `out/../wasm/`.

import esbuild, { type BuildOptions } from "esbuild";

const shared: BuildOptions = {
  bundle   : true,
  platform : "node",
  format   : "cjs",
  target   : "node20",
  sourcemap: true,
  define   : { "import.meta.url": "__bundleUrl" },
  banner: {
    js: 'const __bundleUrl = require("node:url").pathToFileURL(__filename).href;',
  },
  logLevel : "info",
};

const client: BuildOptions = {
  ...shared,
  entryPoints: ["client/extension.ts"],
  outfile    : "out/client.js",
  external   : ["vscode"],
};

const server: BuildOptions = {
  ...shared,
  entryPoints: ["server/server.ts"],
  outfile    : "out/server.js",
};

const watch = process.argv.includes("--watch");
const contexts = await Promise.all([esbuild.context(client), esbuild.context(server)]);
if (watch) {
  await Promise.all(contexts.map((context) => context.watch()));
} else {
  await Promise.all(contexts.map((context) => context.rebuild()));
  await Promise.all(contexts.map((context) => context.dispose()));
}
