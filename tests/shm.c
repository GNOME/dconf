#define _GNU_SOURCE

#include "../common/dconf-paths.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>

#include "../shm/dconf-shm.h"
#include "../shm/dconf-shm-mockable.h"
#include "tmpdir.h"

static void
test_mkdir_fail (void)
{
  guint8 *shm;

  if (g_test_subprocess ())
    {
      gchar *evil;
      gint fd;

      g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

      evil = g_build_filename (g_get_user_runtime_dir (), "dconf", NULL);
      fd = open (evil, O_WRONLY | O_CREAT, 0600);
      close (fd);

      shm = dconf_shm_open ("foo");
      g_assert_null (shm);

      g_unlink (evil);
      g_free (evil);

      return;
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
  g_test_trap_assert_stderr ("*unable to create directory*");
}

static void
test_close_null (void)
{
  dconf_shm_close (NULL);
}

static void
test_open_and_flag (void)
{
  guint8 *shm;

  shm = dconf_shm_open ("foo");
  g_assert_nonnull (shm);
  g_assert_false (dconf_shm_is_flagged (shm));
  dconf_shm_flag ("foo");
  g_assert_true (dconf_shm_is_flagged (shm));
  dconf_shm_close (shm);
}

static void
test_invalid_name (void)
{
  if (g_test_subprocess ())
    {
      guint8 *shm;

      g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

      shm = dconf_shm_open ("foo/bar");
      g_assert_null (shm);
      g_assert_true (dconf_shm_is_flagged (shm));
      return;
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
  g_test_trap_assert_stderr ("*unable to create*foo/bar*");
}

static void
test_flag_nonexistent (void)
{
  dconf_shm_flag ("does-not-exist");
}

static gboolean should_fail_pwrite;
/* interpose */
ssize_t
dconf_shm_pwrite (int fd, const void *buf, size_t count, off_t offset)
{
  if (should_fail_pwrite)
    {
      errno = ENOSPC;
      return -1;
    }

  return pwrite (fd, buf, count, offset);
}

static void
test_out_of_space_open (void)
{
  if (g_test_subprocess ())
    {
      guint8 *shm;

      g_log_set_always_fatal (G_LOG_LEVEL_ERROR);
      should_fail_pwrite = TRUE;

      shm = dconf_shm_open ("foo");
      g_assert_null (shm);
      g_assert_true (dconf_shm_is_flagged (shm));
      return;
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
  g_test_trap_assert_stderr ("*failed to allocate*foo*");
}

static void
test_out_of_space_flag (void)
{
  if (g_test_subprocess ())
    {
      g_log_set_always_fatal (G_LOG_LEVEL_ERROR);
      should_fail_pwrite = TRUE;

      dconf_shm_flag ("foo");
      return;
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
}

int
main (int argc, char **argv)
{
  gchar *temp;
  gint status;

  temp = dconf_test_create_tmpdir ();

  g_setenv ("XDG_RUNTIME_DIR", temp, TRUE);
  /* This currently works, but it is possible that one day GLib will
   * read the XDG_RUNTIME_DIR variable (and cache its value) as a
   * side-effect of the dconf_test_create_tmpdir() call above.
   *
   * This assert will quickly uncover the problem in that case...
   */
  g_assert_cmpstr (g_get_user_runtime_dir (), ==, temp);

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/shm/mkdir-fail", test_mkdir_fail);
  g_test_add_func ("/shm/close-null", test_close_null);
  g_test_add_func ("/shm/open-and-flag", test_open_and_flag);
  g_test_add_func ("/shm/invalid-name", test_invalid_name);
  g_test_add_func ("/shm/flag-nonexistent", test_flag_nonexistent);
  g_test_add_func ("/shm/out-of-space-open", test_out_of_space_open);
  g_test_add_func ("/shm/out-of-space-flag", test_out_of_space_flag);

  status = g_test_run ();

  dconf_test_remove_tmpdir (temp);
  g_free (temp);

  return status;
}
