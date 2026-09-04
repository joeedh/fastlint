const useColor = process.stdout.isTTY === true && !process.env["NO_COLOR"];

function paint(code: string, text: string): string {
  return useColor ? `\x1b[${code}m${text}\x1b[0m` : text;
}

export const color = {
  dim   : (t: string) => paint("2", t),
  bold  : (t: string) => paint("1", t),
  red   : (t: string) => paint("31", t),
  green : (t: string) => paint("32", t),
  yellow: (t: string) => paint("33", t),
  cyan  : (t: string) => paint("36", t),
};

export function info(message: string): void {
  console.log(message);
}

export function step(message: string): void {
  console.log(color.cyan(`> ${message}`));
}

export function warn(message: string): void {
  console.error(color.yellow(`warning: ${message}`));
}

export function fail(message: string): never {
  console.error(color.red(`error: ${message}`));
  process.exit(1);
}

/** Prints rows under headers, padding each column to its widest cell. */
export function table(headers: string[], rows: string[][]): void {
  const widths = headers.map((h, i) =>
    Math.max(h.length, ...rows.map((r) => (r[i] ?? "").length))
  );
  const line = (cells: string[]) =>
    cells
      .map((c, i) => c.padEnd(widths[i] ?? 0))
      .join("  ")
      .trimEnd();
  console.log(color.bold(line(headers)));
  console.log(color.dim(line(widths.map((w) => "-".repeat(w)))));
  for (const row of rows) console.log(line(row));
}
