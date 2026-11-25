// The same-directory include target: src/main.c's quoted include names
// util/log.h relative to src/ (the C preprocessor rule).
#ifndef LOG_H
#define LOG_H

int log_line(const char *msg);

#endif
