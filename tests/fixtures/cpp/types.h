// The routing-sniff fixture (SPRINT-21): C++-distinctive content in a
// .h file -- the content sniff (clean cpp parse + a C++-distinctive
// node) routes it to the cpp pack. The named namespace body IS surface
// (the class, the enum class, the alias, the macro); class members and
// enumerator constants are not (the documented boundaries).
#ifndef TYPES_H
#define TYPES_H

namespace app {

class Engine {
public:
    int start(int v);   // member declaration: never surface
    int id = 0;
};

enum class Mode { READ, WRITE };

using Word = unsigned long;

}

#define LIMIT 16

#endif
