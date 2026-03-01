/* Disable MinGW's automatic wildcard expansion in argv.
 * The Winix shell handles glob expansion itself; coreutils receive
 * pre-expanded argument lists and must not re-expand them. */
int _dowildcard = 0;
