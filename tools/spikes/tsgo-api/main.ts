import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { performance } from "node:perf_hooks";
import { resolveTscExe } from "./lib/exe.ts";
import {
  JsonRpcClient,
  MsgpackClient,
  type Json,
  type Rpc,
  type SpawnOptions,
} from "./lib/rpc.ts";
import {
  Session,
  type SnapshotResponse,
  type SymbolResponse,
  type TypeResponse,
} from "./lib/session.ts";
import {
  canonicalPath,
  decodeSourceFile,
  findNode,
  nodeHandle,
} from "./lib/sourceFile.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const fixture = path.join(here, "fixture");
const fixtureConfig = path.join(fixture, "tsconfig.json");
const fixtureMain = path.join(fixture, "src", "main.ts");
const midsize = "C:/dev/TypeScript/packages/typescript";
const midsizeConfig = path.join(midsize, "tsconfig.json");

/** ast.KindCallExpression as numbered by the installed 7.0.2 binary. */
const KIND_CALL_EXPRESSION = 214;

type Transport = "jsonrpc" | "msgpack";

function connect(transport: Transport, opts: SpawnOptions): Rpc {
  return transport === "jsonrpc" ? new JsonRpcClient(opts) : new MsgpackClient(opts);
}

async function timed<T>(fn: () => Promise<T>): Promise<[T, number]> {
  const start = performance.now();
  const value = await fn();
  return [value, performance.now() - start];
}

function ms(n: number): string {
  return `${n.toFixed(1)}ms`;
}

function stats(samples: number[]): {
  n: number;
  min: number;
  median: number;
  p95: number;
  mean: number;
} {
  const sorted = [...samples].sort((a, b) => a - b);
  const at = (q: number) =>
    sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))]!;
  return {
    n     : sorted.length,
    min   : sorted[0]!,
    median: at(0.5),
    p95   : at(0.95),
    mean  : samples.reduce((a, b) => a + b, 0) / samples.length,
  };
}

/** Byte offsets of identifier-looking tokens, which are what a rule asks about. */
function identifierPositions(text: string, limit: number): number[] {
  const out: number[] = [];
  const re = /[A-Za-z_$][A-Za-z0-9_$]*/g;
  for (let m = re.exec(text); m && out.length < limit; m = re.exec(text)) {
    out.push(m.index);
  }
  return out;
}

const results: Record<string, Json> = {};

function reporter(record: Record<string, Json>, width = 34) {
  return (label: string, value: Json): void => {
    record[label] = value;
    const text = JSON.stringify(value) ?? "undefined";
    console.log(
      `  ${label.padEnd(width)} ${text.length > 300 ? `${text.slice(0, 300)}… (${text.length} chars)` : text}`
    );
  };
}

// ---------------------------------------------------------------- queries

/**
 * Runs the queries the type-aware rules of task 6.3 need, so the response
 * shapes and the follow-up hops are on the record rather than inferred from
 * proto.go.
 */
async function phaseQueries(exe: string): Promise<void> {
  console.log("\n== queries ==");
  const rpc = connect("jsonrpc", { exe, cwd: fixture });
  const session = await Session.open(rpc, fixtureConfig);
  const src = fs.readFileSync(fixtureMain, "utf8");

  const at = (needle: string, offset = 0): number => {
    const i = src.indexOf(needle);
    if (i < 0) throw new Error(`fixture is missing ${JSON.stringify(needle)}`);
    return i + offset;
  };
  const checker = (method: string, params: Record<string, Json>): Promise<Json> =>
    rpc.call(method, {
      snapshot: session.snapshot,
      project : session.project.id,
      ...params,
    });

  const record: Record<string, Json> = {};
  const show = reporter(record);

  // no-floating-promises reaches the promise through the callee's signature,
  // because a position names a token and never the call expression itself.
  const calleeType = (await session.typeAtPosition(fixtureMain, at("fetchUser(id);")))!;
  show("callee type", calleeType);
  show("callee typeToString", await checker("typeToString", { type: calleeType.id }));
  show("callee symbol", await session.ofObject("getSymbolOfType", calleeType.id));
  const signatures = (await checker("getSignaturesOfType", {
    type: calleeType.id,
    kind: 0,
  })) as { id: number }[];
  show("call signatures", signatures);
  const returnType = (await checker("getReturnTypeOfSignature", {
    signature: signatures[0]!.id,
  })) as TypeResponse;
  show("return type", returnType);
  show("return typeToString", await checker("typeToString", { type: returnType.id }));
  show(
    "signature parameters",
    await checker("getParametersOfSignature", { objectId: signatures[0]!.id })
  );

  // strict-boolean-expressions and no-unnecessary-condition need union members
  // and nullability.
  const maybeType = (await session.typeAtPosition(
    fixtureMain,
    at("maybe !== undefined")
  ))!;
  show("union type", maybeType);
  show("union members", await session.ofObject("getTypesOfType", maybeType.id));
  show("nonNullable", await checker("getNonNullableType", { type: maybeType.id }));

  // The no-unsafe-* family keys off TypeFlags.Any (1).
  const anyType = (await session.typeAtPosition(
    fixtureMain,
    at("anything.whatever", "anything.".length)
  ))!;
  show("any-flowed type", anyType);

  // array-type wants array-likeness plus the element type.
  const tagsType = (await session.typeAtPosition(
    fixtureMain,
    at("of tags", "of ".length)
  ))!;
  show("array type", tagsType);
  show("isArrayLikeType", await checker("isArrayLikeType", { type: tagsType.id }));
  const props = (await checker("getPropertiesOfType", { type: tagsType.id })) as {
    name: string;
  }[];
  show("array property count", props.length);
  show(
    "array property names",
    props.slice(0, 6).map((p) => p.name)
  );

  // Assignability, used by no-unsafe-argument and friends.
  const stringType = (await checker("getStringType", {})) as TypeResponse;
  const literalType = (await session.typeAtPosition(
    fixtureMain,
    at('"literal" as const')
  ))!;
  show("string intrinsic", stringType);
  show("const literal type", literalType);
  show(
    "isTypeAssignableTo(literal, string)",
    await checker("isTypeAssignableTo", {
      source: literalType.id,
      target: stringType.id,
    })
  );

  // Provenance: symbol -> declarations -> file, which v2 invalidation keys on.
  show(
    "symbol",
    await session.symbolAtPosition(fixtureMain, at("user.name", "user.".length))
  );

  results["queries"] = record;
  await session.release();
  await rpc.close();
}

// ---------------------------------------------------------------- compat

/**
 * The installed binary and the Go checkout disagree about which parameter name
 * carries a type id, so the spelling each endpoint accepts is probed rather
 * than read off proto.go.
 */
async function phaseCompat(exe: string): Promise<void> {
  console.log("\n== parameter spellings accepted by this build ==");
  const rpc = connect("jsonrpc", { exe, cwd: fixture });
  const session = await Session.open(rpc, fixtureConfig);
  const src = fs.readFileSync(fixtureMain, "utf8");
  const subject = (await session.typeAtPosition(
    fixtureMain,
    src.indexOf("maybe !== undefined")
  ))!;

  const methods = [
    "getSymbolOfType",
    "getTypesOfType",
    "getAliasSymbolOfType",
    "getAliasTypeArgumentsOfType",
    "getNonNullableType",
    "getBaseTypeOfLiteralType",
    "getWidenedType",
    "getApparentType",
    "getPropertiesOfType",
    "getBaseTypes",
    "getIndexInfosOfType",
    "isArrayType",
    "isArrayLikeType",
    "typeToString",
    "getReducedType",
  ];

  const record: Record<string, Json> = {};
  for (const method of methods) {
    const outcome: Record<string, string> = {};
    for (const key of ["objectId", "type"]) {
      try {
        await rpc.call(method, {
          snapshot: session.snapshot,
          project : session.project.id,
          [key]     : subject.id,
        });
        outcome[key] = "ok";
      } catch (e) {
        const msg = (e as Error).message;
        outcome[key] = msg.includes("empty type handle") ? "not read" : msg.slice(0, 48);
      }
    }
    record[method] = outcome;
    const accepted = Object.entries(outcome).find(([, v]) => v === "ok")?.[0] ?? "-";
    console.log(
      `  ${method.padEnd(30)} ${accepted.padEnd(9)} ${JSON.stringify(outcome)}`
    );
  }

  // Which of the checkout's methods this build actually serves. An "unknown
  // API method" reply means the method is absent; any other error means it
  // exists and merely rejected the empty parameters.
  const known: string[] = [];
  const missing: string[] = [];
  const listed = fs
    .readFileSync(path.join(here, "methods.txt"), "utf8")
    .split(/\r?\n/)
    .filter((line) => line.length > 0);
  for (const method of listed) {
    try {
      await rpc.call(method, {});
      known.push(method);
    } catch (e) {
      if ((e as Error).message.includes("unknown API method")) missing.push(method);
      else known.push(method);
    }
  }
  console.log(
    `\n  methods in the checkout: ${listed.length}, served here: ${known.length}`
  );
  console.log(`  absent from 7.0.2: ${missing.join(", ")}`);
  record["methodsInCheckout"] = listed.length;
  record["methodsServed"] = known.length;
  record["methodsMissing"] = missing;

  results["compat"] = record;
  await session.release();
  await rpc.close();
}

// ---------------------------------------------------------------- handles

/**
 * Expression-level queries need a NodeHandle, and a handle carries the node's
 * index in tsgo's own encoded parse tree. This decodes that tree, synthesizes a
 * handle for a call expression, and checks how long the handle stays valid.
 */
async function phaseHandles(exe: string): Promise<void> {
  console.log("\n== node handles ==");
  const rpc = connect("msgpack", { exe, cwd: fixture });
  const session = await Session.open(rpc, fixtureConfig);
  const record: Record<string, Json> = {};
  const show = reporter(record, 30);

  const [encoded, fetchTime] = await timed(() => session.sourceFile(fixtureMain));
  const raw = encoded as { rawBytes?: number } | null;
  show("getSourceFile (msgpack)", { bytes: raw?.rawBytes ?? null, fetch: fetchTime });

  // The msgpack transport answers getSourceFile with raw bytes, which the JSON
  // path base64-encodes instead; the JSON path is used here so the spike does
  // not need a second decoder.
  const jsonRpc = connect("jsonrpc", { exe, cwd: fixture });
  const jsonSession = await Session.open(jsonRpc, fixtureConfig);
  const [sfResponse, jsonFetch] = await timed(() => jsonSession.sourceFile(fixtureMain));
  const data = Buffer.from((sfResponse as { data: string }).data, "base64");
  const file = decodeSourceFile(data);
  show("getSourceFile (jsonrpc)", {
    base64Bytes : (sfResponse as { data: string }).data.length,
    decodedBytes: data.length,
    fetch       : jsonFetch,
  });
  show("encoded file", {
    protocolVersion: file.protocolVersion,
    nodeCount      : file.nodeCount,
    bytesPerNode   : file.bytes / file.nodeCount,
  });

  const src = fs.readFileSync(fixtureMain, "utf8");
  const floating = "  fetchUser(id);";
  const callStart = src.indexOf(floating) + 2;
  const callEnd = callStart + "fetchUser(id)".length;
  const call = findNode(file, callStart, callEnd, KIND_CALL_EXPRESSION);
  if (!call) throw new Error("no CallExpression covering the floating call");
  const handle = nodeHandle(call, canonicalPath(fixtureMain));
  show("synthesized handle", handle);

  const callType = await jsonSession.typeAtLocation(handle);
  show("getTypeAtLocation", callType);
  show(
    "typeToString",
    await jsonRpc.call("typeToString", {
      snapshot: jsonSession.snapshot,
      project : jsonSession.project.id,
      type    : (callType as TypeResponse).id,
    })
  );

  // A handle minted against one snapshot, used against the next.
  const next = (await jsonRpc.call("updateSnapshot", {})) as SnapshotResponse;
  const nextSession = jsonSession.withSnapshot(
    next.snapshot,
    next.projects[0] ?? jsonSession.project
  );
  try {
    show("handle on a later snapshot", await nextSession.typeAtLocation(handle));
  } catch (e) {
    show("handle on a later snapshot", `ERROR ${(e as Error).message}`);
  }

  results["handles"] = record;
  await session.release();
  await rpc.close();
  await nextSession.release();
  await jsonSession.release();
  await jsonRpc.close();
}

// ---------------------------------------------------------------- latency

interface TransportMeasurement {
  transport: Transport;
  spawnToInitialize: number;
  openProject: number;
  firstQuery: number;
  sequential: ReturnType<typeof stats>;
  batched: { calls: number; total: number; perQuery: number } | undefined;
  positionsBatch: { calls: number; total: number; perQuery: number };
  pipelined?: { total: number; perQuery: number };
  bytesSent: number;
  bytesReceived: number;
}

async function measureTransport(
  exe: string,
  transport: Transport,
  projectDir: string,
  configFile: string,
  queryFile: string,
  queryCount: number
): Promise<TransportMeasurement> {
  const rpc = connect(transport, { exe, cwd: projectDir });
  const [, spawnToInitialize] = await timed(() => rpc.call("initialize", null));
  const [snap, openProject] = await timed(
    () =>
      rpc.call("updateSnapshot", {
        openProjects: [configFile],
      }) as Promise<SnapshotResponse>
  );
  const project = snap.projects[0];
  if (!project) throw new Error(`no project for ${configFile}`);
  const session = new Session(rpc, snap.snapshot, project);

  const text = fs.readFileSync(queryFile, "utf8");
  const positions = identifierPositions(text, queryCount);

  const [, firstQuery] = await timed(() =>
    session.typeAtPosition(queryFile, positions[0]!)
  );

  const samples: number[] = [];
  for (const pos of positions) {
    const [, dt] = await timed(() => session.typeAtPosition(queryFile, pos));
    samples.push(dt);
  }

  const batchParams = positions.map((position) => ({
    method: "getTypeAtPosition",
    params: {
      snapshot: session.snapshot,
      project : project.id,
      file    : queryFile,
      position,
    },
  }));
  let batched: TransportMeasurement["batched"];
  const beforeBatch = rpc.stats.calls;
  try {
    const [, batchTotal] = await timed(() => session.batch(batchParams));
    batched = {
      calls   : rpc.stats.calls - beforeBatch,
      total   : batchTotal,
      perQuery: batchTotal / positions.length,
    };
  } catch {
    batched = undefined;
  }

  const beforePositions = rpc.stats.calls;
  const [, positionsTotal] = await timed(() =>
    session.typesAtPositions(queryFile, positions)
  );
  const positionsBatch = {
    calls   : rpc.stats.calls - beforePositions,
    total   : positionsTotal,
    perQuery: positionsTotal / positions.length,
  };

  let pipelined: TransportMeasurement["pipelined"];
  if (transport === "jsonrpc") {
    const [, total] = await timed(() =>
      Promise.all(positions.map((pos) => session.typeAtPosition(queryFile, pos)))
    );
    pipelined = { total, perQuery: total / positions.length };
  }

  const measurement: TransportMeasurement = {
    transport,
    spawnToInitialize,
    openProject,
    firstQuery,
    sequential: stats(samples),
    batched,
    positionsBatch,
    pipelined,
    bytesSent    : rpc.stats.bytesSent,
    bytesReceived: rpc.stats.bytesReceived,
  };

  await session.release();
  await rpc.close();
  return measurement;
}

function printMeasurement(m: TransportMeasurement): void {
  console.log(`  [${m.transport}]`);
  console.log(`    spawn -> initialize   ${ms(m.spawnToInitialize)}`);
  console.log(`    updateSnapshot(open)  ${ms(m.openProject)}`);
  console.log(`    first type query      ${ms(m.firstQuery)}`);
  const s = m.sequential;
  console.log(
    `    per query (n=${s.n})     min ${ms(s.min)} median ${ms(s.median)} p95 ${ms(s.p95)} mean ${ms(s.mean)}`
  );
  console.log(
    m.batched
      ? `    batchRequests         ${ms(m.batched.total)} total, ${ms(m.batched.perQuery)} per query`
      : "    batchRequests         unavailable in this build"
  );
  console.log(
    `    getTypesAtPositions   ${ms(m.positionsBatch.total)} total, ${ms(m.positionsBatch.perQuery)} per query`
  );
  if (m.pipelined) {
    console.log(
      `    pipelined             ${ms(m.pipelined.total)} total, ${ms(m.pipelined.perQuery)} per query`
    );
  }
  console.log(`    bytes  sent ${m.bytesSent}  received ${m.bytesReceived}`);
}

async function phaseBench(exe: string): Promise<void> {
  console.log("\n== bench: fixture (2 files) ==");
  const small: TransportMeasurement[] = [];
  for (const transport of ["jsonrpc", "msgpack"] as const) {
    const m = await measureTransport(
      exe,
      transport,
      fixture,
      fixtureConfig,
      fixtureMain,
      100
    );
    small.push(m);
    printMeasurement(m);
  }
  results["fixture"] = small as unknown as Json;

  if (!fs.existsSync(midsizeConfig)) {
    console.log(`\n(skipping the mid-size bench: ${midsizeConfig} not found)`);
    return;
  }
  console.log("\n== bench: packages/typescript (108 files, ~35k lines) ==");
  const big: TransportMeasurement[] = [];
  const queryFile = path.join(midsize, "src", "api", "async", "api.ts");
  for (const transport of ["jsonrpc", "msgpack"] as const) {
    const m = await measureTransport(
      exe,
      transport,
      midsize,
      midsizeConfig,
      queryFile,
      200
    );
    big.push(m);
    printMeasurement(m);
  }
  results["midsize"] = big as unknown as Json;
}

// ---------------------------------------------------------------- edits

/** Measures what an edit to one file costs, which sets the watch-mode budget. */
async function phaseUpdate(exe: string): Promise<void> {
  console.log("\n== updateSnapshot after an edit ==");
  const scratch = path.join(fixture, "src", "scratch.ts");
  fs.writeFileSync(scratch, 'export const value: string = "a";\n');
  try {
    const rpc = connect("jsonrpc", { exe, cwd: fixture });
    const session = await Session.open(rpc, fixtureConfig);
    const pos = "export const ".length;
    await session.typeAtPosition(scratch, pos);

    const record: Record<string, Json> = {};

    // A temporary snapshot layers new text over one file without touching disk.
    try {
      const [temp, tempTime] = await timed(
        () =>
          rpc.call("updateTemporarySnapshot", {
            snapshot: session.snapshot,
            file    : scratch,
            newText : "export const value: number = 1;\n",
          }) as Promise<SnapshotResponse>
      );
      const tempSession = session.withSnapshot(
        temp.snapshot,
        temp.projects?.[0] ?? session.project
      );
      const [tempType, tempQuery] = await timed(() =>
        tempSession.typeAtPosition(scratch, pos)
      );
      record["updateTemporarySnapshot"] = tempTime;
      record["queryOnTemporarySnapshot"] = tempQuery;
      record["temporaryType"] = tempType as Json;
      console.log(
        `  updateTemporarySnapshot ${ms(tempTime)}, first query ${ms(tempQuery)} -> ${JSON.stringify(tempType)}`
      );
      await tempSession.release();
    } catch (e) {
      record["updateTemporarySnapshot"] = `unavailable: ${(e as Error).message}`;
      console.log(`  updateTemporarySnapshot unavailable: ${(e as Error).message}`);
    }

    // A real edit on disk, announced through fileChanges.
    fs.writeFileSync(scratch, "export const value: number = 2;\n");
    const [next, updateTime] = await timed(
      () =>
        rpc.call("updateSnapshot", {
          fileChanges: { changed: [scratch] },
        }) as Promise<SnapshotResponse>
    );
    const nextSession = session.withSnapshot(
      next.snapshot,
      next.projects[0] ?? session.project
    );
    const [changedType, changedQuery] = await timed(() =>
      nextSession.typeAtPosition(scratch, pos)
    );
    record["updateSnapshot"] = updateTime;
    record["queryAfterUpdate"] = changedQuery;
    record["changes"] = next.changes ?? null;
    console.log(
      `  updateSnapshot(changed) ${ms(updateTime)}, first query ${ms(changedQuery)} -> ${JSON.stringify(changedType)}`
    );
    console.log(`  changes: ${JSON.stringify(next.changes)}`);

    // An unrelated file should still answer from the retained program.
    const [, untouched] = await timed(() => nextSession.typeAtPosition(fixtureMain, 30));
    record["queryUntouchedFile"] = untouched;
    console.log(`  query in an untouched file ${ms(untouched)}`);

    results["update"] = record;
    await nextSession.release();
    await session.release();
    await rpc.close();
  } finally {
    fs.rmSync(scratch, { force: true });
  }
}

// ---------------------------------------------------------------- callbacks

/** Checks whether the server will take file contents from us instead of the disk. */
async function phaseCallbacks(exe: string): Promise<void> {
  console.log("\n== FS callbacks ==");
  const served = new Map<string, string>();
  const norm = (p: string) => p.replace(/\\/g, "/").toLowerCase();
  for (const name of ["tsconfig.json", "src/users.ts"]) {
    served.set(
      norm(path.join(fixture, name)),
      fs.readFileSync(path.join(fixture, name), "utf8")
    );
  }
  // The text served for main.ts differs from what is on disk, so a type read
  // back from it proves the override took effect.
  served.set(norm(fixtureMain), "export const fromCallback: 42 = 42;\n");

  let readFileCalls = 0;
  let servedCalls = 0;
  const rpc = connect("jsonrpc", {
    exe,
    cwd       : fixture,
    extraArgs : ["--callbacks=readFile,fileExists"],
    onCallback: (method, params) => {
      const p = params as { path?: string; fileName?: string } | string;
      const raw = typeof p === "string" ? p : p.path ?? p.fileName ?? "";
      if (method === "readFile") {
        readFileCalls++;
        const text = served.get(norm(raw));
        if (text === undefined) return null;
        servedCalls++;
        return { content: text };
      }
      if (method === "fileExists") return served.has(norm(raw)) ? true : null;
      return null;
    },
  });

  const session = await Session.open(rpc, fixtureConfig);
  const type = await session.typeAtPosition(fixtureMain, "export const ".length);
  console.log(`  readFile callbacks ${readFileCalls}, served from memory ${servedCalls}`);
  console.log(`  first declaration in the overridden main.ts: ${JSON.stringify(type)}`);
  results["callbacks"] = { readFileCalls, servedCalls, type: type as Json };
  await session.release();
  await rpc.close();
}

// ---------------------------------------------------------------- main

const phases = new Set(process.argv.slice(2));
const wants = (name: string) => phases.size === 0 || phases.has(name);

const exe = await resolveTscExe();
console.log(`tsc: ${exe}`);

if (wants("queries")) await phaseQueries(exe);
if (wants("compat")) await phaseCompat(exe);
if (wants("handles")) await phaseHandles(exe);
if (wants("update")) await phaseUpdate(exe);
if (wants("callbacks")) await phaseCallbacks(exe);
if (wants("bench")) await phaseBench(exe);

const out = path.join(process.cwd(), ".cache", "tsgo-api-spike.json");
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, JSON.stringify(results, null, 2));
console.log(`\nwrote ${out}`);
