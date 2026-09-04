/**
 * Minimal reader for the encoded source file returned by `getSourceFile`.
 * Only the header and the node table are decoded, which is all that is needed
 * to turn a (kind, pos, end) triple into the node index a NodeHandle carries.
 * The format is documented in tsc/internal/api/encoder/encoder.go.
 */

const HEADER_SIZE = 44;
const NODE_SIZE = 28;

export interface EncodedNode {
  index: number;
  kind: number;
  pos: number;
  end: number;
  next: number;
  parent: number;
  flags: number;
}

/** Marks a node-list pseudo-node in the table rather than a real AST node. */
export const KIND_NODE_LIST = 0xffffffff;

export interface EncodedSourceFile {
  protocolVersion: number;
  contentHash: bigint;
  bytes: number;
  nodeCount: number;
  nodes: EncodedNode[];
}

export function decodeSourceFile(data: Buffer): EncodedSourceFile {
  // The version occupies the high byte of the first word in 7.0.2, though
  // encoder.go documents it at byte 0; read the whole word and take whichever
  // byte is set.
  const protocolVersion = data.readUInt8(0) || data.readUInt8(3);
  const contentHash = data.readBigUInt64LE(4) ^ (data.readBigUInt64LE(12) << 64n);
  const nodesOffset = data.readUInt32LE(40);
  const nodeCount = (data.length - nodesOffset) / NODE_SIZE;
  const nodes: EncodedNode[] = [];
  for (let i = 0; i < nodeCount; i++) {
    const at = nodesOffset + i * NODE_SIZE;
    nodes.push({
      index : i,
      kind  : data.readUInt32LE(at),
      pos   : data.readUInt32LE(at + 4),
      end   : data.readUInt32LE(at + 8),
      next  : data.readUInt32LE(at + 12),
      parent: data.readUInt32LE(at + 16),
      flags : data.readUInt32LE(at + 24),
    });
  }
  return { protocolVersion, contentHash, bytes: data.length, nodeCount, nodes };
}

export { HEADER_SIZE, NODE_SIZE };

/** The narrowest node whose span covers [pos, end) and whose kind matches, if given. */
export function findNode(
  file: EncodedSourceFile,
  pos: number,
  end: number,
  kind?: number
): EncodedNode | undefined {
  let best: EncodedNode | undefined;
  for (const node of file.nodes) {
    if (node.kind === 0 || node.kind === KIND_NODE_LIST) continue;
    if (node.pos > pos || node.end < end) continue;
    if (kind !== undefined && node.kind !== kind) continue;
    if (!best || node.end - node.pos < best.end - best.pos) best = node;
  }
  return best;
}

/** The handle string the server parses back into a node: `index.kind.path`. */
export function nodeHandle(node: EncodedNode, filePath: string): string {
  return `${node.index}.${node.kind}.${filePath}`;
}

/** tsgo's canonical path form on a case-insensitive file system. */
export function canonicalPath(fileName: string): string {
  return fileName.replace(/\\/g, "/").toLowerCase();
}
