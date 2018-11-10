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


def dconf_read(key):
    return dconf('read', key).stdout.rstrip('\n')


def dconf_write(key, value):
    dconf('write', key, value)


def dconf_list(key):
    return dconf('list', key).stdout.splitlines()


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

        os.environ['XDG_RUNTIME_DIR'] = self.runtime_dir
        os.environ['XDG_CONFIG_HOME'] = self.config_home

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
        """Reseting whole directory is possible with -f option.

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


if __name__ == '__main__':
    # Make sure we don't pick up mandatory profile.
    mandatory_profile = '/run/dconf/user/{}'.format(os.getuid())
    assert not os.path.isfile(mandatory_profile)

    # Avoid profile sourced from environment
    os.environ.pop('DCONF_PROFILE', None)

    if len(sys.argv) < 3:
        message = 'Usage: {} path-to-dconf path-to-dconf-service'.format(
            sys.argv[0])
        raise RuntimeError(message)

    dconf_exe, dconf_service_exe = sys.argv[1:3]
    del sys.argv[1:3]

    # Run tests!
    unittest.main()
