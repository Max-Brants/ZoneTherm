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


# Hook the image file, not the buildfs/uploadfs aliases: SCons runs an alias's
# actions after the nodes it depends on are already built, so hooking the alias
# packed data/ *before* vite wrote to it - one build stale locally, and empty in
# CI where data/ is gitignored and starts out absent. uploadfs depends on the
# same node, so this covers both targets.
env.AddPreAction("$BUILD_DIR/littlefs.bin", build_webui)
