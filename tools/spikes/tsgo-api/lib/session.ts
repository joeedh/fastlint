import type { Json, Rpc } from "./rpc.ts";

export interface ProjectResponse {
  id: string;
  configFileName: string;
  [key: string]: Json;
}

export interface SnapshotResponse {
  snapshot: number;
  projects: ProjectResponse[];
  changes?: Json;
}

export interface TypeResponse {
  id: number;
  flags: number;
  objectFlags?: number;
  value?: Json;
  target?: number;
  symbol?: number;
  intrinsicName?: string;
  [key: string]: Json;
}

export interface SymbolResponse {
  id: number;
  project: string;
  name: string;
  flags: number;
  checkFlags: number;
  declarations?: string[];
  valueDeclaration?: string;
  [key: string]: Json;
}

/** Brings a server up to the point where type queries can be issued. */
export class Session {
  readonly rpc: Rpc;
  readonly snapshot: number;
  readonly project: ProjectResponse;

  constructor(rpc: Rpc, snapshot: number, project: ProjectResponse) {
    this.rpc = rpc;
    this.snapshot = snapshot;
    this.project = project;
  }

  static async open(rpc: Rpc, tsconfig: string): Promise<Session> {
    await rpc.call("initialize", null);
    const snap = (await rpc.call("updateSnapshot", {
      openProjects: [tsconfig],
    })) as SnapshotResponse;
    const project = snap.projects[0];
    if (!project) throw new Error(`no project loaded for ${tsconfig}`);
    return new Session(rpc, snap.snapshot, project);
  }

  /** Reopens against a newer snapshot id, keeping the same project. */
  withSnapshot(snapshot: number, project?: ProjectResponse): Session {
    return new Session(this.rpc, snapshot, project ?? this.project);
  }

  private base(file: string) {
    return { snapshot: this.snapshot, project: this.project.id, file };
  }

  typeAtPosition(file: string, position: number): Promise<TypeResponse | null> {
    return this.rpc.call("getTypeAtPosition", {
      ...this.base(file),
      position,
    }) as Promise<TypeResponse | null>;
  }

  typesAtPositions(file: string, positions: number[]): Promise<(TypeResponse | null)[]> {
    return this.rpc.call("getTypesAtPositions", {
      ...this.base(file),
      positions,
    }) as Promise<(TypeResponse | null)[]>;
  }

  symbolAtPosition(file: string, position: number): Promise<SymbolResponse | null> {
    return this.rpc.call("getSymbolAtPosition", {
      ...this.base(file),
      position,
    }) as Promise<SymbolResponse | null>;
  }

  /**
   * A sub-property request such as getTypesOfType or getNonNullableType. The
   * generic sub-property endpoints all name their subject `objectId`, unlike
   * the checker endpoints, which spell it `type` or `source`/`target`.
   */
  ofObject(method: string, objectId: number): Promise<Json> {
    return this.rpc.call(method, {
      snapshot: this.snapshot,
      project : this.project.id,
      objectId,
    });
  }

  typeAtLocation(location: string): Promise<TypeResponse | null> {
    return this.rpc.call("getTypeAtLocation", {
      snapshot: this.snapshot,
      project : this.project.id,
      location,
    }) as Promise<TypeResponse | null>;
  }

  sourceFile(file: string): Promise<Json> {
    return this.rpc.call("getSourceFile", {
      snapshot: this.snapshot,
      project : this.project.id,
      file,
    });
  }

  batch(requests: { method: string; params: Json }[]): Promise<{ responses: Json[] }> {
    return this.rpc.call("batchRequests", { requests }) as Promise<{ responses: Json[] }>;
  }

  release(): Promise<Json> {
    return this.rpc.call("release", { snapshot: this.snapshot });
  }
}
