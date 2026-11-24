// Corpus A (SPRINT-11): the module declaration parses cleanly and
// carries NO function symbols (contract caveat §Java.1, pinned by this
// fixture); OC002 still consumes its comments verbatim.
module com.example.app {
    requires java.base;
}
