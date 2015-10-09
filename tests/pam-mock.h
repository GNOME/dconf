#include <stdarg.h>
#include <stdio.h>

typedef int pam_handle_t;

#define LOG_ERR 0
#define LOG_NOTICE 0
#define LOG_DEBUG 0
#define PAM_SUCCESS 0

#define PAM_EXTERN

PAM_EXTERN int
pam_sm_open_session (pam_handle_t  *pamh,
                     int            flags,
                     int            argc,
                     const char   **argv);


PAM_EXTERN int
pam_sm_close_session (pam_handle_t  *pamh,
                      int            flags,
                      int            argc,
                      const char   **argv);


void
pam_syslog (pam_handle_t *pamh,
            int           priority,
            const char   *fmt,
            ...);

int
pam_get_user (pam_handle_t  *pamh,
              const char   **user,
              const char    *prompt);
const char*
pam_getenv (pam_handle_t *pamh,
            const char   *name);
void
pam_mock_set_xdg_data_dirs (char * data_dirs);

void
pam_mock_set_xdg_runtime_dir (char * runtime_dir);

void
pam_mock_set_user (char * user);
