#!/usr/bin/env python3

import os
import subprocess
import sys

if not os.environ.get('DESTDIR'):
  print('GIO module cache creation...')
  subprocess.call(['gio-querymodules', sys.argv[1]])
