import { Card, ns } from "ui";
const items = [1];
const view = <Card title={items.length} {...ns.props}>
  <ns.Row />
  <div>{items}</div>
</Card>;
