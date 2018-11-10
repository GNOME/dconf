#!/usr/bin/env python3

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
    kwargs.setdefault('stderr', subprocess.PIPE)
    kwargs.setdefault('universal_newlines', True)

    return subprocess.run(argv, **kwargs)


def dconf_read(key):
    return dconf('read', key).stdout.rstrip('\n')


def dconf_write(key, value):
    dconf('write', key, value)


def dconf_list(key):
    lines = dconf('list', key).stdout.splitlines()
    # FIXME: Change dconf to produce sorted output.
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

        self.runtime_dir = os.path.join(self.temporary_dir.name, 'config')
        self.config_home = os.path.join(self.temporary_dir.name, 'run')
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

    def test_read(self):
        value = dconf_read('/key')
        self.assertEqual(value, '')

        dconf('write', '/key', '0')
        value = dconf_read('/key')
        self.assertEqual(value, '0')

        dconf('write', '/key', '"hello there"')
        value = dconf_read('/key')
        self.assertEqual(value, "'hello there'")

    def test_list(self):
        dconf('write', '/org/gnome/app/fullscreen', 'true')
        dconf('write', '/org/gnome/terminal/profile', '"default"')
        dconf('write', '/key', '42')

        items = dconf_list('/')
        self.assertEqual(items, ['key', 'org/'])

        items = dconf_list('/org/')
        self.assertEqual(items, ['gnome/'])

        items = dconf_list('/no-existing/directory/')
        self.assertEqual(items, [])

    def test_reset(self):
        dconf('write', '/app/width', '1024')
        dconf('write', '/app/height', '768')
        dconf('write', '/app/fullscreen', 'true')

        # Sanity check.
        items = dconf_list('/app/')
        self.assertEqual(items, ['fullscreen', 'height', 'width'])

        # Reset a single key.
        dconf('reset', '/app/fullscreen')
        items = dconf_list('/app/')
        self.assertEqual(items, ['height', 'width'])

        # To reset a directory we need -f.
        with self.assertRaises(subprocess.CalledProcessError) as cm:
            dconf('reset', '/app/')
        self.assertRegex(cm.exception.stderr, '-f must be given')
        self.assertRegex(cm.exception.stderr, 'Usage:')

        dconf('reset', '-f', '/app/')

        # Everything should be gone now.
        items = dconf_list('/')
        self.assertEqual(items, [])

    def test_watch(self):
        watch_root = dconf_watch('/')
        watch_org = dconf_watch('/org/')

        time.sleep(0.2)

        dconf('write', '/com/a', '1')
        dconf('write', '/org/b', '2')
        dconf('write', '/c', '3')

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

        /c
          3

        '''
        self.assertEqual(watch_root.stdout.read(), dedent(expected))

        # Watch for '/org/' should capture only a subset.
        expected = '''\
        /org/b
          2

        '''
        self.assertEqual(watch_org.stdout.read(), dedent(expected))

    def test_dump_load(self):
        # FIXME: This test depends on:
        # * order of groups
        # * order of items within groups
        # Change dconf to produce output in sorted order.

        keyfile = dedent('''\
        [/]
        password='secret'

        [org/editor/language/c-sharp]
        tab-width=8

        [org/editor/language/c]
        tab-width=2

        [org/editor]
        window-size=(1280, 977)
        ''')

        # Load and dump is identity.
        dconf('load', '/', input=keyfile)
        self.assertEqual(dconf('dump', '/').stdout, keyfile)

        # Copy /org/ directory to /com/.
        keyfile = dconf('dump', '/org/').stdout
        dconf('load', '/com/', input=keyfile)

        # Verify result.
        keyfile_org = dconf('dump', '/org/').stdout
        keyfile_com = dconf('dump', '/com/').stdout
        self.assertEqual(keyfile_org, keyfile_com)


if __name__ == '__main__':
    # Make sure we don't pick up mandatory profile.
    mandatory_profile = '/run/dconf/usr/{}'.format(os.getuid())
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
