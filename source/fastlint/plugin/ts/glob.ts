// The config glob matcher (task 8.2), a port of lint/glob.cc. The two sides read
// the same `files` and `ignores` patterns, so they have to agree on what a
// pattern means; glob.test.ts runs the cases lint_config_test.cc runs.

/** Windows compares paths without regard to case, so a glob matches that way too. */
export const pathCaseInsensitive = process.platform === "win32";

/** Lowercases the ASCII range only, as the C++ matcher does. */
function lower(c: string): string {
  return c >= "A" && c <= "Z" ? String.fromCharCode(c.charCodeAt(0) + 32) : c;
}

function charsEqual(a: string, b: string, caseInsensitive: boolean): boolean {
  return caseInsensitive ? lower(a) === lower(b) : a === b;
}

function matchExpanded(p: string, s: string, caseInsensitive: boolean): boolean {
  while (p.length > 0) {
    if (p.startsWith("**")) {
      // `**` eats whole segments: try the rest at every segment boundary.
      let rest = p.slice(2);
      if (rest.startsWith("/")) rest = rest.slice(1);
      if (rest.length === 0) return true;
      for (let k = 0; ; ) {
        if (matchExpanded(rest, s.slice(k), caseInsensitive)) return true;
        const slash = s.indexOf("/", k);
        if (slash < 0) return false;
        k = slash + 1;
      }
    }
    const c = p[0]!;
    if (c === "*") {
      // Zero or more characters within the current segment.
      for (let k = 0; ; k++) {
        if (matchExpanded(p.slice(1), s.slice(k), caseInsensitive)) return true;
        if (k >= s.length || s[k] === "/") return false;
      }
    }
    if (s.length === 0) {
      // `dir/**` also names `dir` itself.
      return (
        c === "/" &&
        p.startsWith("/**") &&
        matchExpanded(p.slice(3), s, caseInsensitive)
      );
    }
    if (c === "?") {
      if (s[0] === "/") return false;
    } else if (!charsEqual(c, s[0]!, caseInsensitive)) {
      return false;
    }
    p = p.slice(1);
    s = s.slice(1);
  }
  return s.length === 0;
}

/** Expands the first `{a,b}` group of `pattern` and matches every alternative. */
function matchBraces(pattern: string, path: string, caseInsensitive: boolean): boolean {
  const open = pattern.indexOf("{");
  if (open < 0) return matchExpanded(pattern, path, caseInsensitive);
  const close = pattern.indexOf("}", open);
  if (close < 0) return matchExpanded(pattern, path, caseInsensitive);
  const head = pattern.slice(0, open);
  const tail = pattern.slice(close + 1);
  for (const alternative of pattern.slice(open + 1, close).split(",")) {
    if (matchBraces(head + alternative + tail, path, caseInsensitive)) return true;
  }
  return false;
}

/**
 * Matches `path` (forward slashes, relative to the config) against a glob with
 * `*` (within a segment), `?`, `**` (any number of segments) and `{a,b}`
 * alternatives. Dotfiles match `*` as they do in ESLint.
 */
export function globMatch(
  pattern: string,
  path: string,
  caseInsensitive = false
): boolean {
  return matchBraces(
    pattern.startsWith("./") ? pattern.slice(2) : pattern,
    path,
    caseInsensitive
  );
}

/**
 * `filename` as the globs see it: forward slashes, and relative to `baseDir`
 * when it sits underneath. Mirrors `Config::relativePath`.
 */
export function relativePath(baseDir: string, filename: string): string {
  const path = filename.replace(/\\/g, "/");
  let base = baseDir.replace(/\\/g, "/");
  if (base.length > 0 && !base.endsWith("/")) base += "/";
  if (base.length > 0 && path.length > base.length && path.startsWith(base)) {
    return path.slice(base.length);
  }
  return path;
}
