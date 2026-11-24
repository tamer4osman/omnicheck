// Integration test data: a .rs file directly inside tests/ is runner
// data (cargo compiles each as its own crate) -- a zero in-degree is
// expected and never OC006 material. The `mycorp::` head names the
// SIBLING crate: external-crate claim, unresolved (the manifest is
// never read -- the decoy above declares it, and the resolver must not
// care).
use mycorp::store::types::Widget;

#[test]
fn widget_roundtrip() {
    let _w = Widget::new();
}
