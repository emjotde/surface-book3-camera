# Ubuntu build definition

`ubuntu.yml` preserves the Ubuntu 24.04 build recipe for the Surface driver,
ISP and GStreamer adapter, script validation and artifact checks.

It is intentionally outside `.github/workflows/`, so GitHub Actions is not
enabled. The existing Copilot login can publish repository contents but lacks
the additional `workflow` scope needed to create or update active workflows.
The full original upstream history, including its former CI definition, is
still preserved.

The matching source snapshot has been built locally against the tested kernel,
and scripts passed ShellCheck. This does not claim a GitHub-hosted CI run.
To enable GitHub Actions later, authorize workflow access and move
`ubuntu.yml` to `.github/workflows/ci.yml` in a separate commit.
