#pragma once

// Rewritten by the release workflow from the git tag before building
// (.github/workflows/release.yml stamps the literal below).
//
// It MUST match the tag it ships in. UpdateService compares this string
// against the newest tag published on GitHub, so a value that lags the tag
// leaves every device convinced the release is still pending - it would
// download and reinstall the same image on every check.
#define FW_VERSION "1.0.1"
