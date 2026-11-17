## Summary

<!-- What does this change, and why? Link the issue it fixes, e.g. "Fixes #123". -->

## Testing

<!-- What you ran and played. Tick what applies and say what you saw. -->

- [ ] Built on Windows (`setup.ps1`, or CI's Windows build)
- [ ] Played alone (not in a room)
- [ ] Played in a room: <!-- how many players, host or joiner, NebulaNet or direct -->
- [ ] Unit tests / CI only (no game run needed)

## Checklist

- [ ] I did **not** hand-bump the game API, co-op protocol or wire version numbers. They are build-time hashes and
      change by themselves.
- [ ] If this changes Nebula's own code (not co-op), I considered whether it belongs upstream in
      [keepvaibin/nebula](https://github.com/keepvaibin/nebula) instead.
- [ ] If this changes netcode, downloads from the host, or what a joiner's PC accepts, `SECURITY.md`'s scope still
      matches.
- [ ] Docs (`README.md`, `docs/`, `TODO.md`) are updated where behaviour changed.
