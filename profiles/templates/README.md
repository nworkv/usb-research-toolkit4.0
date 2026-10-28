# Bundled USB 2.0 profile templates

These are generic class/protocol templates for an isolated lab, not copies of named commercial devices.

| File | Profile category | Runtime status |
|---|---|---|
| `vendor-bulk.json` | Vendor-specific interface with bulk IN/OUT | Intended as the first working test profile; host driver binding still required for vendor-specific interfaces |
| `cdc-acm-serial.json` | CDC ACM serial adapter | Descriptor/control-request example; verify on the actual UDC, Linux host and Windows 10 |
| `hid-mouse.json` | HID relative mouse, zero-motion reports | Descriptor/report example; emits stationary reports; no keyboard emulation |
| `msc-bot-descriptor-only.json` | Mass-storage Bulk-Only Transport descriptors | Descriptor-only; NOT a functional storage device because SCSI and BOT CBW/CSW handling are not implemented |

All bundled VID/PID values are illustrative. Use only a VID/PID allocated or otherwise authorized for your test. The common PID prefix is kept stable to make generated variants easy to compare, but generated profiles are not guaranteed to be compatible with every UDC or host driver.

Use `usb-profile-gen list` to display this catalog and `usb-profile-gen show <name>` to print the full JSON and compiled descriptor bytes.
