// What the client and the server say to each other beyond LSP (task 9.2). The
// settings are read by the server through `workspace/configuration`; this file
// names their shape once, and the two notifications the status bar rests on.

/** The `lintrix.*` settings, as `package.json` declares them. */
export interface Settings {
  enable: boolean;
  run: "onType" | "onSave";
  /** Language ids the extension lints. */
  validate: string[];
  engine: "auto" | "native" | "wasm";
  /** A native `lintrix` for the serve mode (task 9.5); null leaves it to the
   * config's `binary` item and PATH. */
  binaryPath: string | null;
  /** The native `tsc` the native engine types with; null resolves it from the
   * project, as the CLI does. */
  tsgoPath: string | null;
  codeActionsOnSave: {
    mode: "all" | "problems";
  };
}

export const defaultSettings: Settings = {
  enable           : true,
  run              : "onType",
  validate         : ["javascript", "javascriptreact", "typescript", "typescriptreact"],
  engine           : "auto",
  binaryPath       : null,
  tsgoPath         : null,
  codeActionsOnSave: { mode: "all" },
};

/** Sent by the server after each lint, and once when the engine loads, so the
 * client's status bar item follows the active document. */
export const statusNotification = "lintrix/status";

export interface StatusParams {
  /** The document the status is about, or absent for the server as a whole. */
  uri?: string;
  state: "ok" | "warning" | "error";
  /** Which engine linted, shown in the item. */
  engine?: string;
  /** What went wrong, shown in the tooltip and logged. */
  message?: string;
}

/** Sent by the client on `lintrix.revalidate`: drop every cache and lint the
 * open documents again. */
export const revalidateNotification = "lintrix/revalidate";
