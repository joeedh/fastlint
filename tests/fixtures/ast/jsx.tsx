const a = <div className="x" data-id={id} {...rest} disabled>text {value} <br /></div>;
const b = <>
  <Foo.Bar<T> prop="p" />
  <svg:rect ns:attr="v" />
  <this.Comp>{/* only a comment */}</this.Comp>
  {...items}
  {}
</>;
const c = <A b=<C /> d={<>x</>}></A>;
const d = <Unclosed>
