/*
 * CLI entry point. Kept as a function (not main) so the integration
 * tests can drive the exact same code path as users do.
 */
#ifndef OC_CLI_H
#define OC_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

int oc_cli_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif
#endif /* OC_CLI_H */
