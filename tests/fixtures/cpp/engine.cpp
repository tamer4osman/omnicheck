// The symbol-shape fixture (SPRINT-21): the inline method is a symbol
// (field_identifier), the out-of-class definition is a symbol through
// the qualified_identifier name field, and the class members never
// become module surface (the class is the surface unit).
struct Widget {
    int size(void) { return 4; }
};

int Widget::size(void) { return 4; }

int engine_user(void) {
    Widget w;
    return w.size();
}
