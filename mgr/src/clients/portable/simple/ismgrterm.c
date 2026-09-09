/*
 * ismgrterm - returns 0 (true) if tty is an MGR window.
 */

extern int is_mgr_term();

int
main()
{
  return ! is_mgr_term();
}
