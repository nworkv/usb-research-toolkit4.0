# Architecture notes

## Layers

1. `Json` is a small self-contained parser/writer to keep the three principal tools buildable without third-party JSON dependencies.
2. `profile.cpp` transforms a profile JSON into descriptor byte sequences and validates ranges, counts and common USB 2.0 constraints.
3. `raw_gadget_backend_linux.cpp` is the platform adapter to `/dev/raw-gadget` and the Linux `USB_RAW_IOCTL_*` API.
4. `TraceLogger` writes one JSON object per line from both the control path and endpoint worker threads.
5. `usb-trace` reports event counts and correlates exact control SETUP fingerprints or transfer fingerprints.

## Data flow

`profile JSON -> validate -> compile bytes -> Raw Gadget setup request -> descriptor/control response -> endpoint transfer worker -> JSONL trace`.

The test-host application sends bulk OUT payloads and requests bulk IN data using libusb where it is available. External USBPcap/usbmon capture can be normalized to JSONL by `normalize-capture.sh`.

## Concurrency

A single Raw Gadget event loop receives UDC-binding, control, suspend/resume, reset and disconnect events. The Raw Gadget CONNECT event is not itself proof that host enumeration completed. Each enabled bulk/interrupt endpoint has a worker thread: OUT endpoints queue reads; IN endpoints queue the configured static payload. Endpoint disable tries to cancel outstanding operations before joining workers. Hardware/UDC behavior varies; verify reset/unplug cancellation on the actual kernel/board.

## Descriptor policies

- `calculated_fields: auto`: canonical fixed descriptor types/lengths and calculated descriptor count/length fields are used.
- `calculated_fields: manual`: supplied values for calculated lengths/counts are preserved. Use only with research profiles for intentionally inconsistent fields.
- `raw_descriptors`: keys such as `"6:0"` map descriptor type/index to raw hex bytes, enabling GET_DESCRIPTOR of device qualifier or another custom descriptor. Raw descriptors are restricted to Research mode by validation.
- `extra_descriptors_hex`: raw descriptor bytes attached after the relevant Configuration, Interface or Endpoint descriptor; use it for class-specific/IAD/HID-style extra structures where byte order/length fields are supplied explicitly.
- `control_responses`: optional array of exact vendor/class control-IN responses. Entries match `bmRequestType`, `bRequest`, and optionally `wValue`/`wIndex`.

## Deliberate v0.1 limitations

- No isochronous transfer worker.
- Non-default alternate settings are compiled but endpoint runtime activates alternate setting 0 only; `SET_INTERFACE` is logged, not applied.
- `GET_STATUS` returns a zero status word; class requests need explicit responses.
- No arbitrary OUT-control callback DSL; data is captured/logged, but semantics are not configurable.
- No full USBPcap/usbmon packet decoder; the capture helper requires tshark and uses fields available in the installed Wireshark version.
- Hardware support cannot be conclusively validated from a profile alone. Runtime `EP_ENABLE` is the final compatibility check.
