# HORI Fighting Stick α (Xbox) input fixes

## Guide / CMD release on normal input

Xbox One input (GIP 0x20) and Guide (GIP 0x07) arrive separately.
The driver cleared the whole pad state on every normal input report, releasing
Guide even while held. Preserve only the Guide bit across normal input reports;
its own release report still clears it. The user confirmed CMD + button rapid-fire
switching works on hardware after this fix.

## Mode 1 false Share input

Observed device: VID:PID `0f0d:0153`, decoded as XBOXONE.
With A held alone, decoded buttons were `0x1800` (A + Share), and the receive
buffer's byte 22 was `0x10`. With B held alone, decoded buttons were `0x2800`
(B + Share), and byte 22 was `0x20`. At each snapshot the most recent packet was
an 8-byte 0x03 status report, so byte 22 was retained from an earlier transfer,
not part of that status packet.

The normal input parser used `rdata[22] && 0x01`, treating any nonzero byte as
Share. It now tests bit 0 with `&`, and only reads it when the received report
contains byte 22. The regression test exercises A/B without Share, actual Share,
Share + A, short reports with stale data, and the previous Guide hold/release fix:

```sh
python3 tests/xinput_guide_test.py
cmake --build build -j 4
```

Button configurations saved while false Share was present may contain Share
alongside every assigned button. Reconfigure the controller after updating;
the driver fix intentionally does not rewrite saved mappings.

The updated 273024-byte image was written and verified with OpenOCD. Settings
and macro storage (0x10180000–0x101fffff) matched byte-for-byte before and after
writing. The previous 273032-byte firmware was also read back and matched its
saved binary before writing. Stop at a main-loop flash address when backing up:
`getBootButton()` temporarily disables XIP, so arbitrary halts can return invalid
flash reads. Physical button verification after this update is pending.
