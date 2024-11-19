#!/usr/bin/env python3
#
# Copyright © 2018 Tomasz Miąsko
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the licence, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.

import mmap
import os
import subprocess
import sys
import tempfile
import time
import unittest

from textwrap import dedent

DAEMON_CONFIG = '''
<!DOCTYPE busconfig PUBLIC
 "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <keep_umask/>
  <listen>unix:tmpdir=/tmp</listen>
  <servicedir>{servicedir}</servicedir>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
'''

SERVICE_CONFIG = '''\
[D-BUS Service]
Name={name}
Exec={exec}
'''


def dconf(*args, **kwargs):
    argv = [dconf_exe]
    argv.extend(args)

    # Setup convenient defaults:
    kwargs.setdefault('check', True)
    kwargs.setdefault('stdout', subprocess.PIPE)
    kwargs.setdefault('universal_newlines', True)

    return subprocess.run(argv, **kwargs)


def dconf_read(key, **kwargs):
    return dconf('read', key, **kwargs).stdout.rstrip('\n')


def dconf_write(key, value):
    dconf('write', key, value)


def dconf_list(key):
    return dconf('list', key).stdout.splitlines()

def dconf_locks(key, **kwargs):
    lines = dconf('list-locks', key, **kwargs).stdout.splitlines()
    lines.sort()
    return lines

def dconf_complete(suffix, prefix):
    lines = dconf('_complete', suffix, prefix).stdout.splitlines()
    lines.sort()
    return lines


def dconf_watch(path):
    args = [dconf_exe, 'watch', path]
    return subprocess.Popen(args,
                            stdout=subprocess.PIPE,
                            universal_newlines=True)


class DBusTest(unittest.TestCase):

    def setUp(self):
        self.temporary_dir = tempfile.TemporaryDirectory()

        self.runtime_dir = os.path.join(self.temporary_dir.name, 'run')
        self.config_home = os.path.join(self.temporary_dir.name, 'config')
        self.dbus_dir = os.path.join(self.temporary_dir.name, 'dbus-1')

        os.mkdir(self.runtime_dir, mode=0o700)
        os.mkdir(self.config_home, mode=0o700)
        os.mkdir(self.dbus_dir, mode=0o700)
        os.mkdir(os.path.join(self.config_home, 'dconf'))

        os.environ['DCONF_PROFILE'] = os.path.join(self.temporary_dir.name, 'profile')
        os.environ['DCONF_BLAME'] = ''
        os.environ['XDG_RUNTIME_DIR'] = self.runtime_dir
        os.environ['XDG_CONFIG_HOME'] = self.config_home

        # Configure default profile so that the system one
        # does not affect the tests.
        path = os.path.join(self.temporary_dir.name, 'profile')
        with open(path, 'w') as file:
            file.write('user-db:user')

        # Prepare dbus-daemon config.
        dbus_daemon_config = os.path.join(self.dbus_dir, 'session.conf')
        with open(dbus_daemon_config, 'w') as file:
            file.write(DAEMON_CONFIG.format(servicedir=self.dbus_dir))

        # Prepare service config.
        name = 'ca.desrt.dconf'
        path = os.path.join(self.dbus_dir, '{}.service'.format(name))
        with open(path, 'w') as file:
            config = SERVICE_CONFIG.format(name=name, exec=dconf_service_exe)
            file.write(config)

        # Pipe where daemon will write its address.
        read_fd, write_fd = os.pipe2(0)

        args = ['dbus-daemon',
                '--config-file={}'.format(dbus_daemon_config),
                '--nofork',
                '--print-address={}'.format(write_fd)]

        # Start daemon
        self.dbus_daemon_process = subprocess.Popen(args, pass_fds=[write_fd])

        # Close our writing end of pipe. Daemon closes its own writing end of
        # pipe after printing address, so subsequent reads shouldn't block.
        os.close(write_fd)

        with os.fdopen(read_fd) as f:
            dbus_address = f.read().rstrip()

        # Prepare environment
        os.environ['DBUS_SESSION_BUS_ADDRESS'] = dbus_address

    def tearDown(self):
        # Terminate dbus-daemon.
        p = self.dbus_daemon_process
        try:
            p.terminate()
            p.wait(timeout=0.5)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()

        self.temporary_dir.cleanup()

    def test_invalid_usage(self):
        """Invalid dconf usage results in non-zero exit code and help message.
        """
        cases = [
            # No command:
            [],

            # Invalid command:
            ['no-such-command'],

            # Too many arguments:
            ['blame', 'a'],

            # Missing arguments:
            ['compile'],
            ['compile', 'output'],
            # Too many arguments:
            ['compile', 'output', 'dir1', 'dir2'],

            # Missing arguments:
            ['_complete'],
            ['_complete', ''],
            # Too many arguments:
            ['_complete', '', '/', '/'],

            # Missing argument:
            ['dump'],
            # Dir is required:
            ['dump', '/key'],
            # Too many arguments:
            ['dump', '/a/', '/b/'],

            # Missing argument:
            ['list'],
            # Dir is required:
            ['list', '/foo/bar'],
            # Too many arguments:
            ['list', '/foo', '/bar'],

            # Missing argument:
            ['list-locks'],
            # Dir is required:
            ['list-locks', '/key'],
            # Too many arguments:
            ['list-locks', '/a/', '/b/'],

            # Missing argument:
            ['load'],
            # Dir is required:
            ['load', '/key'],
            # Too many arguments:
            ['load', '/a/', '/b/'],

            # Missing argument:
            ['read'],
            # Key is required:
            ['read', '/dir/'],
            # Too many arguments:
            ['read', '/a', '/b'],
            ['read', '-d', '/a', '/b'],

            # Missing arguments:
            ['reset'],
            # Invalid path:
            ['reset', 'test/test'],
            # Too many arguments:
            ['reset', '/test', '/test'],
            ['reset', '-f', '/', '/'],

            # Missing arguments:
            ['watch'],
            # Invalid path:
            ['watch', 'foo'],
            # Too many arguments:
            ['watch', '/a', '/b'],

            # Missing arguments:
            ['write'],
            ['write', '/key'],
            # Invalid value:
            ['write', '/key', 'not-a-gvariant-value'],
            # Too many arguments:
            ['write', '/key', '1', '2'],

            # Too many arguments:
            ['update', 'a', 'b'],
        ]

        for args in cases:
            with self.subTest(args=args):
                with self.assertRaises(subprocess.CalledProcessError) as cm:
                    dconf(*args, stderr=subprocess.PIPE)
                self.assertRegex(cm.exception.stderr, 'Usage:')

    def test_help(self):
        """Help show usage information on stdout and exits with success."""

        stdout = dconf('help', 'write').stdout
        self.assertRegex(stdout, 'dconf write KEY VALUE')

        stdout = dconf('help', 'help').stdout
        self.assertRegex(stdout, 'dconf help COMMAND')

    def test_read_nonexisiting(self):
        """Reading missing key produces no output. """

        self.assertEqual('', dconf_read('/key'))

    def test_write_read(self):
        """Read returns previously written value."""

        dconf('write', '/key', '0')
        self.assertEqual('0', dconf_read('/key'))

        dconf('write', '/key', '"hello there"')
        self.assertEqual("'hello there'", dconf_read('/key'))

    def test_list(self):
        """List returns a list of names inside given directory.

        Results include both keys and subdirectories.
        """

        dconf('write', '/org/gnome/app/fullscreen', 'true')
        dconf('write', '/org/gnome/terminal/profile', '"default"')
        dconf('write', '/key', '42')

        self.assertEqual(['key', 'org/'], dconf_list('/'))

        self.assertEqual(['gnome/'], dconf_list('/org/'))

    def test_list_missing(self):
        """List can be used successfully with non existing directories. """

        self.assertEqual([], dconf_list('/no-existing/directory/'))

    def test_reset_key(self):
        """Reset can be used to reset a value of a single key."""

        dconf('write', '/app/width', '1024')
        dconf('write', '/app/height', '768')
        dconf('write', '/app/fullscreen', 'true')

        # Sanity check.
        self.assertEqual(['fullscreen', 'height', 'width'], dconf_list('/app/'))

        # Reset one key after another:
        dconf('reset', '/app/fullscreen')
        self.assertEqual(['height', 'width'], dconf_list('/app/'))
        dconf('reset', '/app/width')
        self.assertEqual(['height'], dconf_list('/app/'))
        dconf('reset', '/app/height')
        self.assertEqual([], dconf_list('/app/'))

    def test_reset_dir(self):
        """Resetting whole directory is possible with -f option.

        It is an error not to use -f when resetting a dir.
        """

        dconf('write', '/app/a', '1')
        dconf('write', '/app/b', '2')
        dconf('write', '/app/c/d', '3')
        dconf('write', '/x', '4')
        dconf('write', '/y/z', '5')

        with self.assertRaises(subprocess.CalledProcessError) as cm:
            dconf('reset', '/app/', stderr=subprocess.PIPE)
        self.assertRegex(cm.exception.stderr, '-f must be given')
        self.assertRegex(cm.exception.stderr, 'Usage:')

        # Nothing should be removed just yet.
        self.assertTrue(['a', 'b', 'c'], dconf_list('/app/'))

        # Try again with -f.
        dconf('reset', '-f', '/app/')

        # /app/ should be gone now:
        self.assertEqual(['x', 'y/'], dconf_list('/'))

    def test_watch(self):
        """Watch reports changes made using write command.

        Only changes made inside given subdirectory should be reported.
        """

        watch_root = dconf_watch('/')
        watch_org = dconf_watch('/org/')

        # Arbitrary delay to give "dconf watch" time to set-up the watch.
        # In the case this turns out to be problematic, dconf tool could be
        # changed to produce debug message after `dconf_client_watch_sync`,
        # so that we could synchronize on its output.

        time.sleep(0.2)

        dconf('write', '/com/a', '1')
        dconf('write', '/org/b', '2')
        dconf('write', '/organ/c', '3')
        dconf('write', '/c', '4')

        # Again, give "dconf watch" some time to pick-up changes.

        time.sleep(0.2)

        watch_root.terminate()
        watch_org.terminate()

        watch_root.wait()
        watch_org.wait()

        # Watch for '/' should capture all writes.
        expected = '''\
        /com/a
          1

        /org/b
          2

        /organ/c
          3

        /c
          4

        '''
        self.assertEqual(dedent(expected), watch_root.stdout.read())

        # Watch for '/org/' should capture only a subset of all writes:
        expected = '''\
        /org/b
          2

        '''
        self.assertEqual(dedent(expected), watch_org.stdout.read())

    def test_dump_load(self):
        """Checks that output produced with dump can be used with load and
        vice versa.
        """
        keyfile = dedent('''\
        [/]
        password='secret'

        [org/editor]
        window-fullscreen=true
        window-size=(1024, 768)

        [org/editor/language/c-sharp]
        tab-width=8

        [org/editor/language/c]
        tab-width=2
        ''')

        # Load and dump is identity.
        dconf('load', '/', input=keyfile)
        self.assertEqual(dconf('dump', '/').stdout, keyfile)

        # Copy /org/ directory to /com/.
        keyfile = dconf('dump', '/org/').stdout
        dconf('load', '/com/', input=keyfile)

        # Verify that /org/ and /com/ are now exactly the same.
        keyfile_org = dconf('dump', '/org/').stdout
        keyfile_com = dconf('dump', '/com/').stdout
        self.assertEqual(keyfile_org, keyfile_com)

    def test_complete(self):
        """Tests _complete command used internally to implement bash completion.

        Runs completion queries after loading a sample database from key-file.
        """

        keyfile = dedent('''\
        [org]
        calamity=false

        [org/calculator]
        window-position=(0, 0)

        [org/calendar]
        window-position=(0, 0)

        [org/history]
        file0='/tmp/a'
        file1='/tmp/b'
        file2='/tmp/c'
        ''')

        dconf('load', '/', input=keyfile)

        # Empty string is completed to '/'.
        completions = dconf_complete('', '')
        self.assertEqual(completions, ['/'])
        completions = dconf_complete('/', '')
        self.assertEqual(completions, ['/'])

        # Invalid paths don't return any completions.
        completions = dconf_complete('', 'foo/')
        self.assertEqual(completions, [])
        completions = dconf_complete('/', 'foo/')
        self.assertEqual(completions, [])

        # Key completions include trailing whitespace,
        # directory completions do not.
        completions = dconf_complete('', '/org/')
        self.assertEqual(completions,
                         ['/org/calamity ',
                          '/org/calculator/',
                          '/org/calendar/',
                          '/org/history/'])

        # Only matches with given prefix are returned.
        completions = dconf_complete('', '/org/cal')
        self.assertEqual(completions,
                         ['/org/calamity ',
                          '/org/calculator/',
                          '/org/calendar/'])

        # Only matches with given suffix are returned.
        completions = dconf_complete('/', '/org/cal')
        self.assertEqual(completions,
                         ['/org/calculator/',
                          '/org/calendar/'])

    def test_compile_precedence(self):
        """Compile processes key-files in reverse lexicographical order.

        When key occurs in multiple files, the value from file processed first
        is preferred.
        
        Test that by preparing four key-files each with a different value for
        '/org/file'. Compiling it directly into user database, and performing
        read to check which value had been selected.
        """
        # Prepare key file database directory.
        user_d = os.path.join(self.temporary_dir.name, 'user.d')
        os.mkdir(user_d, mode=0o700)

        def write_config_d(name):
            keyfile = dedent('''
            [org]
            file = {name}
            '''.format(name=name))

            with open(os.path.join(user_d, name), 'w') as file:
                file.write(keyfile)

        write_config_d('00')
        write_config_d('25')
        write_config_d('50')
        write_config_d('99')

        # Compile directly into user configuration file.
        dconf('compile',
              os.path.join(self.config_home, 'dconf', 'user'),
              user_d)

        # Lexicographically last value should win:
        self.assertEqual(dconf_read('/org/file'), '99')

    def test_redundant_disk_writes(self):
        """Redundant disk writes are avoided.

        When write or reset operations don't modify actual contents of the
        database, the database file shouldn't be needlessly rewritten. Check
        mtime after each redundant operation to verify that.
        """

        config = os.path.join(self.config_home, 'dconf', 'user')

        def move_time_back(path):
            """Moves file mtime 60 seconds back and returns its new value.

            Used to avoid false positives during comparison checks in the case
            that mtime is stored with low precision.
            """
            atime = os.path.getatime(config)
            mtime = os.path.getmtime(config)

            os.utime(config, times=(atime, mtime - 60))

            return os.path.getmtime(config)

        # Activate service to trigger initial database write.
        dconf_write('/prime', '5')

        # Sanity check that database is rewritten when necessary.
        saved_mtime = move_time_back(config)
        dconf_write('/prime', '13')
        self.assertLess(saved_mtime, os.path.getmtime(config))

        # Write the same value as one already in the database.
        saved_mtime = move_time_back(config)
        dconf('write', '/prime', '13')
        self.assertEqual(saved_mtime, os.path.getmtime(config))

        # Reset not directory which is not present in the database.
        saved_mtime = move_time_back(config)
        dconf('reset', '-f', '/non-existing/directory/')
        self.assertEqual(saved_mtime, os.path.getmtime(config))

    def test_compile_dotfiles(self):
        """Compile ignores files starting with a dot."""

        user_d = os.path.join(self.temporary_dir.name, 'user.d')
        os.mkdir(user_d)

        a_conf = dedent('''\
        [math]
        a=42
        ''')

        a_conf_swp = dedent('''\
        [math]
        b=13
        ''')

        with open(os.path.join(user_d, 'a.conf'), 'w') as file:
            file.write(a_conf)

        with open(os.path.join(user_d, '.a.conf.swp'), 'w') as file:
            file.write(a_conf_swp)

        dconf('compile',
              os.path.join(self.config_home, 'dconf', 'user'),
              user_d)

        self.assertEqual(a_conf, dconf('dump', '/').stdout)

    def test_database_invalidation(self):
        """Update invalidates previous database by overwriting the header with
        null bytes.
        """

        db = os.path.join(self.temporary_dir.name, 'db')
        local = os.path.join(db, 'local')
        local_d = os.path.join(db, 'local.d')

        os.makedirs(local_d)

        with open(os.path.join(local_d, 'local.conf'), 'w') as file:
            file.write(dedent('''\
            [org/gnome/desktop/background]
            picture-uri = 'file:///usr/share/backgrounds/gnome/ColdWarm.jpg'
            '''))

        # Compile database for the first time.
        dconf('update', db)

        with open(local, 'rb') as file:
            with mmap.mmap(file.fileno(), 8, mmap.MAP_SHARED, prot=mmap.PROT_READ) as mm:
                # Sanity check that database is valid.
                self.assertNotEqual(b'\0'*8, mm[:8])

                dconf('update', db)

                # Now database should be marked as invalid.
                self.assertEqual(b'\0'*8, mm[:8])

    def test_update_failure(self):
        """Update should skip invalid configuration directory and continue with
        others. Failure to update one of databases should be indicated with
        non-zero exit code.
        
        Regression test for issue #42.
        """

        # A few different scenarios when loading data from key-file:
        valid_key_file = '[org]\na = 1'

        invalid_key_file = "<html>This isn't a key-file nor valid HTML."

        invalid_group_name = dedent('''\
        [org//no/me]
        a = 2
        ''')

        invalid_key_name = dedent('''\
        [org/gnome]
        b// = 2
        ''')

        invalid_value = dedent('''\
        [org/gnome]
        c = 2x2
        ''')

        db = os.path.join(self.temporary_dir.name, 'db')

        # Database name,     valid, content
        cases = [('site_aa', True,  valid_key_file),
                 ('site_bb', False, invalid_key_file),
                 ('site_cc', False, invalid_group_name),
                 ('site_dd', False, invalid_key_name),
                 ('site_ee', False, invalid_value),
                 ('site_ff', True,  valid_key_file)]

        for (name, is_valid, content) in cases:
            conf_dir = os.path.join(db, '{}.d'.format(name))
            conf_file = os.path.join(conf_dir, '{}.conf'.format(name))

            os.makedirs(conf_dir)

            with open(conf_file, 'w') as file:
                file.write(content)

        # Return code should indicate failure.
        with self.assertRaises(subprocess.CalledProcessError) as cm:
            dconf('update', db, stderr=subprocess.PIPE)

        for (name, is_valid, content) in cases:
            path = os.path.join(db, name)
            if is_valid:
                # This one was valid so db should be written successfully.
                self.assertTrue(os.path.exists(path))
                self.assertNotRegex(cm.exception.stderr, name)
            else:
                # This one was broken so we shouldn't create corresponding db.
                self.assertFalse(os.path.exists(path))
                self.assertRegex(cm.exception.stderr, name)

    def test_locks(self):
        """Key paths can be locked in system databases.

        - Update configures locks based on files found in "locks" subdirectory.
        - Locks can be listed with list-locks command.
        - Locks are enforced during write.
        - Load can ignore changes to locked keys using -f option.
        """

        db = os.path.join(self.temporary_dir.name, 'db')
        profile = os.path.join(self.temporary_dir.name, 'profile')
        site = os.path.join(db, 'site')
        site_d = os.path.join(db, 'site.d')
        site_locks = os.path.join(db, site_d, 'locks')

        os.makedirs(site_locks)

        # For meaningful test of locks we need two sources, first of which
        # should be writable. We will use user-db and file-db.
        with open(profile, 'w') as file:
            file.write(dedent('''\
            user-db:user
            file-db:{}
            '''.format(site)))

        # Environment to use for all dconf client invocations.
        env = dict(os.environ)
        env['DCONF_PROFILE'] = profile

        # Default settings
        with open(os.path.join(site_d, '10-site-defaults'), 'w') as file:
            file.write(dedent('''\
            # Some useful default settings for our site
            [system/proxy/http]
            host='172.16.0.1'
            enabled=true

            [org/gnome/desktop]
            background='company-wallpaper.jpeg'
            '''))

        # Lock proxy settings.
        with open(os.path.join(site_locks, '10-proxy-lock'), 'w') as file:
            file.write(dedent('''\
            # Prevent changes to proxy
            /system/proxy/http/host
            /system/proxy/http/enabled
            /system/proxy/ftp/host
            /system/proxy/ftp/enabled
            '''))

        # Compile site configuration.
        dconf('update', db)

        # Test list-locks:
        self.assertEqual(['/system/proxy/ftp/enabled',
                          '/system/proxy/ftp/host',
                          '/system/proxy/http/enabled',
                          '/system/proxy/http/host'],
                         dconf_locks('/', env=env))

        self.assertEqual(['/system/proxy/http/enabled',
                          '/system/proxy/http/host'],
                         dconf_locks('/system/proxy/http/', env=env))

        self.assertEqual([],
                         dconf_locks('/org/gnome/', env=env))

        # Changing unlocked defaults is fine.
        dconf('write', '/org/gnome/desktop/background',
              '"ColdWarm.jpg"', env=env)

        # It is an error to change locked keys.
        with self.assertRaises(subprocess.CalledProcessError) as cm:
            dconf('write', '/system/proxy/http/enabled', 'false',
                  env=env, stderr=subprocess.PIPE)
        self.assertRegex(cm.exception.stderr, 'non-writable keys')

        keyfile = dedent('''\
        [system/proxy/http]
        enabled=false
        [org/gnome/desktop]
        background='Winter.png'
        ''')

        # Load fails to apply changes if some key is locked ...
        with self.assertRaises(subprocess.CalledProcessError) as cm:
            dconf('load', '/', input=keyfile, env=env, stderr=subprocess.PIPE)
        self.assertRegex(cm.exception.stderr, 'non-writable keys')
        self.assertEqual('true', dconf_read('/system/proxy/http/enabled', env=env))
        self.assertEqual("'ColdWarm.jpg'", dconf_read('/org/gnome/desktop/background', env=env))

        # ..., unless invoked with -f option, then it changes unlocked keys.
        stderr = dconf('load', '-f', '/', input=keyfile, env=env, stderr=subprocess.PIPE).stderr
        self.assertRegex(stderr, 'ignored non-writable key')
        self.assertEqual('true', dconf_read('/system/proxy/http/enabled', env=env))
        self.assertEqual("'Winter.png'", dconf_read('/org/gnome/desktop/background', env=env))

    def test_dconf_blame(self):
        """Blame returns recorded information about write operations.

        Recorded information include sender bus name, sender process id and
        object path the write operations was invoked on.
        """

        p = subprocess.Popen([dconf_exe, 'write', '/prime', '307'])
        p.wait()

        blame = dconf('blame').stdout
        print(blame)

        self.assertRegex(blame, 'Sender: ')
        self.assertRegex(blame, 'PID: {}'.format(p.pid))
        self.assertRegex(blame, 'Object path: /ca/desrt/dconf/Writer/user')

if __name__ == '__main__':
    # Make sure we don't pick up mandatory profile.
    mandatory_profile = '/run/dconf/user/{}'.format(os.getuid())
    assert not os.path.isfile(mandatory_profile)

    # Avoid profile sourced from environment or system data dirs.
    os.environ.pop('DCONF_PROFILE', None)
    os.environ.pop('XDG_DATA_DIRS', None)
    # Avoid interfering with external message buses.
    os.environ['DBUS_SYSTEM_BUS_ADDRESS'] = ''
    os.environ['DBUS_SESSION_BUS_ADDRESS'] = ''

    if len(sys.argv) < 3:
        message = 'Usage: {} path-to-dconf path-to-dconf-service'.format(
            sys.argv[0])
        raise RuntimeError(message)

    dconf_exe, dconf_service_exe = sys.argv[1:3]
    del sys.argv[1:3]

    # Run tests!
    unittest.main()
