"""Builds the Svelte web UI into data/ before PlatformIO packs the filesystem image.

Hooked onto the buildfs/uploadfs targets (see extra_scripts in platformio.ini) so
`pio run -t uploadfs` always ships the current webui/src, not a stale data/.
"""

import os
import subprocess
import sys

Import("env")

WEBUI_DIR = env["PROJECT_DIR"] + "/webui"


def build_webui(source, target, env):
    npm = "npm.cmd" if sys.platform == "win32" else "npm"

    # Only reinstall when node_modules is missing; npm run build is fast enough
    # to run on every buildfs/uploadfs, so always do that part.
    if not os.path.isdir(os.path.join(WEBUI_DIR, "node_modules")):
        if subprocess.call([npm, "install"], cwd=WEBUI_DIR) != 0:
            sys.exit("webui_build: npm install failed")

    if subprocess.call([npm, "run", "build"], cwd=WEBUI_DIR) != 0:
        sys.exit("webui_build: npm run build failed")


env.AddPreAction("buildfs", build_webui)
env.AddPreAction("uploadfs", build_webui)
