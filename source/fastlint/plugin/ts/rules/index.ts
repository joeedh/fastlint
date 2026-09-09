// The example rules as a plugin (task 8.2). A `plugins` entry in a config names
// a module like this one, and each rule is then configured as `prefix/name`.

import { eqeqeq } from "./eqeqeq.ts";
import { noConsole, noDebugger } from "./no-debugger.ts";
import { noEmpty } from "./no-empty.ts";
import { noVar } from "./no-var.ts";

export default {
  rules: {
    eqeqeq,
    "no-console": noConsole,
    "no-debugger": noDebugger,
    "no-empty": noEmpty,
    "no-var": noVar,
  },
};
