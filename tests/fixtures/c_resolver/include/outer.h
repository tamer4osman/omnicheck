// The nested-include target: outer.h includes sub/inner.h relative to
// ITS OWN directory (the C rule: "the directory of the file containing
// the directive"), NOT the including .c file's directory.
#ifndef OUTER_H
#define OUTER_H

#include "sub/inner.h"

int outer_api(void);

#endif
