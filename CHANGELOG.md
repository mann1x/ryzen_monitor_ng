# Version 2.0.5
- New sysinfo routine
- Command line switch to print debug init information
# Version 2.0.4
- Fix for PM hack
- Enabled the disabled cores hack also for reading a dump
- Fix EDC in export when not available from PM table
# Version 2.0.3
- Fix version number
- Fix fake fuse hack
# Version 2.0.2
- Hack to create fake fuse for Family 17h Model 18h (Picasso Athlon 300U)
# Version 2.0.1
- Fixes for Picasso PM table 0x1E0004
# Version 2.0.0
- Added support for Cezanne disabled cores fuse (thx @PJVol)
- Added support to show Curve Optimizer counts
- Added support for exporting metrics to a named pipe using Influx DB line protocol format for telegraf import with tail plugin
- Fixed missing free for SMU obj
- Added support for set and get values; CO counts, PPT, PPTFast, PPTAPU, TDC, TDCSoC, EDC, EDCSoC, STAPM Limit & Time, PPT Time, THM Limit, Scalar, OCMode, Eco, MaxPerf
- Added argparse library
- Added make install and debug target
- Added keystrokes in monitor: q - quit, c - compact view, i - toggle info, e - toggle electrical, o - toggle counts, m - toggle memory, g - toggle gfx, p - toggle power
- Added command line switches to control view toggles
- Added write to dumpfile
- Added dump PM table to screen
- Added tentative support for Lucienne/Renoir PM tables (0x370003, 0x370005)
- Added tentative support for Picasso PM tables (0x1E0004)
- RyenSMU library updated to v0.1.4 (not yet merged at this time, https://gitlab.com/mann1x/ryzen_smu)
- Fixed average/peak core_voltage calculation (SMU max core voltage seems to be broken in some conditions) 