import { greet } from "./greet";

export function Badge(props: { name: string }): JSX.Element {
  return <span className="badge">{greet(props.name)}</span>;
}
