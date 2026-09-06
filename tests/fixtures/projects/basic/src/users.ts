export interface User {
  name: string;
  nickname?: string;
  tags: readonly string[];
  kind: "admin" | "guest";
}

export async function fetchUser(id: string): Promise<User> {
  return { name: id, tags: [], kind: "guest" };
}

export function isAdmin(user: User): user is User & { kind: "admin" } {
  return user.kind === "admin";
}
