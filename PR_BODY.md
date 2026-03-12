# Add `spotify.initscan_disable` option to skip Spotify library scan on startup

## Summary

This change introduces a new configuration option `spotify.initscan_disable` (default: `false`) that allows skipping the Spotify Web API library scan during server initialization. The startup authentication flow (token refresh and `spotify_relogin()`) is preserved, so Spotify playback continues to work even when scanning is disabled.

## Files changed

- `src/conffile.c` — add `initscan_disable` config option
- `src/library/spotify_webapi.c` — respect `initscan_disable` (skip only the scan; keep token refresh + relogin)
- `src/library.c` — ensure the global init-scan loop always invokes sources' `initscan()` handlers
- `owntone.conf.in`, `.dev/devcontainer/data/devcontainer-owntone.conf` — document the option in config template

## Why

On some systems the Spotify Web API library scan can be long or undesirable to run at every server restart. This option provides users the ability to skip that scan while keeping playback functionality available immediately after startup.

## Behavior & compatibility

- Default behavior is unchanged: `initscan_disable = false` by default.
- When `initscan_disable = true`:
  - The server will still refresh the Web API token and attempt `spotify_relogin()` on startup so playback works.
  - The Spotify library scan (which populates saved albums/playlists) is skipped.
  - Manual rescans and OAuth-triggered full rescans still work as before.
- Note: older installed binaries (built before this change) will not recognize the `initscan_disable` option and will log a config parse error ("no such option 'initscan_disable'"). Rebuilding and reinstalling the updated binary is required when enabling this option.

## Testing checklist (what was tested)

- Build and install branch on target device (RockPi).
- Start server with `spotify.initscan_disable = true` in `/etc/owntone.conf`:
  - Confirm the Spotify scan is skipped during init.
  - Confirm `token_refresh()` and `spotify_relogin()` are performed and Spotify playback works (no "session invalid").
- Trigger manual/full rescan via JSON API or UI and confirm Spotify library is populated afterward.
- Verify there are no regressions in other library sources (files, RSS).

## How to test locally (quick steps)

1. Build & install the branch on your target device:

```bash
git checkout spotify/initscan-disable
autoreconf -fi   # only if configure is missing
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var --enable-lastfm
make -j"$(nproc)"
sudo systemctl stop owntone || true
sudo make install
sudo systemctl daemon-reload
sudo systemctl restart owntone
sudo journalctl -u owntone -f
```

2. In `/etc/owntone.conf`, set:

```ini
spotify {
  initscan_disable = true
}
```

3. Start the server and verify logs: the Spotify scan should be skipped but token refresh/relogin should run. Try playing Spotify content — playback should succeed. Trigger a manual rescan and verify library entries appear.

## Notes for reviewers

- This change only adds a non-default config flag and ensures startup auth remains intact. There is no behavioural change unless the option is explicitly enabled in config.
- Please verify that the PR compiles cleanly on supported platforms and that the option is documented in configuration templates.

---

If you want, I can open the PR on GitHub for you and paste this body; say `open` and I'll create the PR title and body and return the link, or run the `gh pr create` command if you prefer CLI creation.