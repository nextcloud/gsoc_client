# MSI installer using WiX Installer Framework

## Introduction

At this point, the installer assumes a pre-assembled ownCloud installation in
a subdirectory 'ownCloud' as well as an up to date version of WiX with bash
wrappers. This will both need to change. The Makefile harvests the files
and creates a simple installers.

## TODOs

At this point: pretty much everything:

- Add Explorer Integrations
  - VS Runtime (Fetch msm module(s), better: put aside)
- Terminate running process on update
- StartMenu
- Auto Start
- Metadata (take from NSIS)
- Research PerUser (ALLUSERS="") obstacles
  - No VS global runtime
  - No HKLM entries (Explorer!)
  - Register COM components (HKLM -> HKLU)
  - Documentation
    - https://msdn.microsoft.com/en-us/library/windows/desktop/dd765197(v=vs.85).aspx
- UI (optional!)
 - Options
- Check if running the whole WiX suite on wine in production is feasable
  - Burn not working
  - Sanity checks not working
- Ensure NSIS params get migrated (MSI properties?)
- Sign MSI
- Translations (https://github.com/sblaisot/wxl-po-tools)
- Add CMake target

Potentially:

- WiX Firewall extension to allow port 80/443 outbound?
- Single out crashreporter into extra component?

## Testing Scenarios

- Test Uninstall
- Test Update
- Test Upgrade (difference?)
- "Update" from NSIS?

