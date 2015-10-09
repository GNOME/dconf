#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pam-mock.h"

static char *xdg_data_dirs = 0;
static char *xdg_runtime_dir = 0;

void
pam_syslog (pam_handle_t *pamh,
            int           priority,
            const char   *fmt,
            ...)
{
  /* We keep this around in case we do want to output the logging info */
  va_list vl;
  va_start (vl, fmt);
  vprintf (fmt, vl);
  va_end (vl);
  printf("\n");
  return;
}

int
pam_get_user (pam_handle_t  *pamh,
              const char   **user,
              const char    *prompt)
{
  *user = USERNAME;
  return PAM_SUCCESS;
}

const char*
pam_getenv (pam_handle_t *pamh,
            const char   *name)
{
  if (strcmp (name, "XDG_DATA_DIRS") == 0)
    return xdg_data_dirs;

  if (strcmp (name, "XDG_RUNTIME_DIR") == 0)
    return xdg_runtime_dir;

  return NULL;
}

void
pam_mock_set_xdg_data_dirs (char * data_dirs)
{
  xdg_data_dirs = data_dirs;
}

void
pam_mock_set_xdg_runtime_dir (char * runtime_dir)
{
  xdg_runtime_dir = runtime_dir;
}
