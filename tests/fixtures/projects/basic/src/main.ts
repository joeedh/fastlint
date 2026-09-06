import { fetchUser, type User } from "./users.ts";

export async function run(id: string): Promise<void> {
  const user = await fetchUser(id);
  report(user);
  fetchUser(id);
  const maybe: string | undefined = user.nickname;
  if (maybe !== undefined) {
    console.log(maybe.toUpperCase());
  }
  const tags: readonly string[] = user.tags;
  for (const tag of tags) {
    console.log(`${tag}`);
  }
}

export function report(user: User): string {
  return user.name;
}

export const anything: any = JSON.parse("{}");
export const widened = anything.whatever;
export const literal = "literal" as const;
export const tuple: [number, string?] = [1];
export type Keys = keyof User;
export type Elem<T> = T extends readonly (infer U)[] ? U : never;
