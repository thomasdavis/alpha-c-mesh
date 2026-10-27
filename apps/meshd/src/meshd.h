/* meshd's main, apart from main() itself, so the tests can run a whole meshd in a child process. */
#ifndef MESHD_H
#define MESHD_H

/* Runs meshd with argv (argv[0] the program); never returns (the process exits with its code). */
_Noreturn int meshd_main(int argc, char **argv);

#endif
