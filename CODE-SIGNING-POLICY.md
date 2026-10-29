# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

This policy covers the Windows installers (`CMPLXTY-<Name>-Setup-<version>-win.exe`) and the plugin binaries (`.vst3`, `.clap`) built from https://github.com/cmplxtymusic/cmplxty-plugins by GitHub Actions on GitHub-hosted runners. Only binaries built from this repository's own source are signed. macOS builds are not covered by this certificate.

## Team roles
- Author / committer: [Sam Arnstein (@cmplxtymusic)](https://github.com/cmplxtymusic). Trusted to change source without extra review.
- Reviewers: [Sam Arnstein (@cmplxtymusic)](https://github.com/cmplxtymusic). Every pull request from anyone who isn't a committer is reviewed before it's merged, with extra attention to build scripts and `.github/workflows/`.
- Approvers: [Sam Arnstein (@cmplxtymusic)](https://github.com/cmplxtymusic). Each release is approved by hand in SignPath before it's signed.

All team members use multi-factor authentication on GitHub and SignPath.

## Privacy policy
This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it, with one exception described here:

- **Update check.** Once every 24 hours, each plugin downloads a small public file (`https://cmplxty.music/plugins/<name>/version.json`) to see if a newer version exists. It sends no account, identifier, usage data or system details, just a normal HTTPS request. Like any web request, the web server sees your IP address. You can turn this off in the plugin's update banner (the "Don't check for updates" switch), and the installer explains it. If the check fails, nothing happens.
- Nothing else is sent anywhere. Presets, samples and images you load stay on your computer.

## Installation and removal
The installer only copies the plugin to the standard VST3/CLAP folders (`C:\Program Files\Common Files\VST3`, `...\CLAP`) and registers an uninstaller. Remove it from Windows Settings > Apps > Installed apps, or run `unins000.exe` in the install folder.

## Reporting
If you believe a signed file breaks this policy, open an issue at https://github.com/cmplxtymusic/cmplxty-plugins/issues or email contact@cmplxty.music. You can also report it to support@signpath.io.
