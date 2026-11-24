/* Internal declarations of the built-in analysis passes. */
#ifndef OC_PASSES_H
#define OC_PASSES_H

#include "oc/pass.h"

extern const oc_pass oc_pass_long_function;
extern const oc_pass oc_pass_todo_comment;
extern const oc_pass oc_pass_import_cycle;
extern const oc_pass oc_pass_complexity;
extern const oc_pass oc_pass_unused_export;
extern const oc_pass oc_pass_unreferenced_module;
extern const oc_pass oc_pass_crap_hotspot;
extern const oc_pass oc_pass_duplicate_code;
extern const oc_pass oc_pass_dep_vuln;
extern const oc_pass oc_pass_licence;
extern const oc_pass oc_pass_secret;

#endif /* OC_PASSES_H */
