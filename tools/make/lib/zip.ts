// A reader for plain zip archives: stored or deflated entries, no encryption, no zip64.
// That covers release archives such as the sqlite amalgamation without a dependency.

import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";

const END_OF_CENTRAL_DIR = 0x06054b50;
const CENTRAL_FILE_HEADER = 0x02014b50;
const LOCAL_FILE_HEADER = 0x04034b50;
const METHOD_STORED = 0;
const METHOD_DEFLATE = 8;

export interface ZipEntry {
  name: string;
  data: Buffer;
}

/** Decodes every file entry in `archive`; directories are skipped. */
export function readZip(archive: Buffer): ZipEntry[] {
  // The end record is the last thing in the file; its comment (rarely present) is what
  // the backwards scan skips.
  let end = -1;
  for (
    let at = archive.length - 22;
    at >= 0 && at >= archive.length - 22 - 0xffff;
    at--
  ) {
    if (archive.readUInt32LE(at) === END_OF_CENTRAL_DIR) {
      end = at;
      break;
    }
  }
  if (end < 0) throw new Error("zip: end of central directory not found");
  const entryCount = archive.readUInt16LE(end + 10);
  let at = archive.readUInt32LE(end + 16);
  const entries: ZipEntry[] = [];
  for (let i = 0; i < entryCount; i++) {
    if (archive.readUInt32LE(at) !== CENTRAL_FILE_HEADER) {
      throw new Error(`zip: bad central directory entry at ${at}`);
    }
    const method = archive.readUInt16LE(at + 10);
    const compressedSize = archive.readUInt32LE(at + 20);
    const nameLength = archive.readUInt16LE(at + 28);
    const extraLength = archive.readUInt16LE(at + 30);
    const commentLength = archive.readUInt16LE(at + 32);
    const localOffset = archive.readUInt32LE(at + 42);
    const name = archive.subarray(at + 46, at + 46 + nameLength).toString("utf8");
    at += 46 + nameLength + extraLength + commentLength;
    if (name.endsWith("/")) continue;

    if (archive.readUInt32LE(localOffset) !== LOCAL_FILE_HEADER) {
      throw new Error(`zip: bad local header for ${name}`);
    }
    const localNameLength = archive.readUInt16LE(localOffset + 26);
    const localExtraLength = archive.readUInt16LE(localOffset + 28);
    const start = localOffset + 30 + localNameLength + localExtraLength;
    const raw = archive.subarray(start, start + compressedSize);
    let data: Buffer;
    if (method === METHOD_STORED) data = Buffer.from(raw);
    else if (method === METHOD_DEFLATE) data = zlib.inflateRawSync(raw);
    else throw new Error(`zip: unsupported compression method ${method} for ${name}`);
    entries.push({ name, data });
  }
  return entries;
}

/**
 * Writes the archive's files under `dir`, dropping `stripPrefix` leading path segments
 * (release zips wrap everything in one top-level folder). Returns the written paths.
 */
export function extractZip(archive: Buffer, dir: string, stripPrefix = 1): string[] {
  const written: string[] = [];
  for (const entry of readZip(archive)) {
    const parts = entry.name.split("/").slice(stripPrefix);
    if (parts.length === 0 || parts.some((p) => p === "..")) continue;
    const target = path.join(dir, ...parts);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, entry.data);
    written.push(target);
  }
  return written;
}
