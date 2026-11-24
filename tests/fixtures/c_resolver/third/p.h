// The ..-pop target: src/main.c's "../third/p.h" normalizes across src/
// and lands here (the lexical .. pop, the PHP include-channel rule).
#ifndef P_H
#define P_H

int p_val(int n);

#endif
