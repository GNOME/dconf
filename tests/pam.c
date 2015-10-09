#include <pam-mock.h>
#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

static gchar *
setup_profile (void)
{
  gchar *tmpdir;

  tmpdir = g_dir_make_tmp ("pam_dconf_test.XXXXXX", NULL);

  if (tmpdir == NULL)
    return NULL;

  pam_mock_set_xdg_data_dirs (SRCDIR "/pamprofile");
  pam_mock_set_xdg_runtime_dir (tmpdir);

  return tmpdir;
}

static void
teardown_profile (gchar *tmpdir)
{
  gchar *profile_link;

  profile_link = g_strconcat (tmpdir, "/dconf.profile", NULL);

  g_remove (profile_link);
  g_remove (tmpdir);

  pam_mock_set_xdg_runtime_dir (NULL);
  pam_mock_set_xdg_data_dirs (NULL);

  g_free (profile_link);
}

static void
test_open_session (void)
{
  gchar     *tmpdir;
  gchar     *profile_link;
  GFile     *link;
  GFileInfo *info;

  tmpdir = setup_profile ();
  g_assert (tmpdir != NULL);

  pam_sm_open_session (NULL, 0, 0, NULL);

  profile_link = g_strconcat (tmpdir, "/dconf.profile", NULL);
  link = g_file_new_for_path (profile_link);

  /* Check if file object was created and exists */
  g_assert (link != NULL);
  g_assert (g_file_query_exists (link, NULL));

  info = g_file_query_info (link,
                            "standard::*",
                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                            NULL, NULL);
  /* Check if symlink exists and points to the right place */
  g_assert (info != NULL);
  g_assert (g_file_info_get_is_symlink (info));
  g_assert (g_strcmp0 (g_file_info_get_symlink_target (info),
                       SRCDIR "/pamprofile/dconf/profile/" USERNAME ".profile") == 0);

  teardown_profile (tmpdir);
  g_object_unref (info);
  g_object_unref (link);
  g_free (tmpdir);
  g_free (profile_link);
}

int
main (int argc, char** argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pam/open_session", test_open_session);

  return g_test_run ();
}
