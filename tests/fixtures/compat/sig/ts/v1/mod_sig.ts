export function typed(a: number): number {
  return a;
}

export function optw(a: number, b?: number): number {
  return a + (b ? b : 0);
}

export function reorder(a: number, b: number): number {
  return a + b;
}
