export function typed(a: string): number {
  return 0;
}

export function optw(a: number, b?: number, c?: number): number {
  return a + (b ? b : 0) + (c ? c : 0);
}

export function reorder(b: number, a: number): number {
  return a + b;
}
