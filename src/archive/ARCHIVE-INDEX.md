# src/archive — esp32-s3-nut-node
<!-- v1.0 | 2026-03-07 -->

Superseded drop-in files archived here for reference.
All files from PROJECT_UPLOAD root have been moved here.

## Version history of key files

### main.c lineage
v9 → v10 → v10_fixed → v11 → v11_fixed → v12 → v13 → v13_1 → v13_2
→ v13_3 → v13_4 → v13_5 → v13_6 → v13_7 → v13_8 → v13_9 → v13_10
→ v13_11 → v13_12 → v14_rebuild → v14_1 → v14_2 → v14_3 → v14_4
→ v14_5 → v14_6 → v14_7 (REVERT-0003/0004/0005)

### ups_usb_hid.c lineage
v14_2_string_descriptor → v14_2_FIXED → v14_5_descriptor_integrated
→ v14_8_1_fixed → v14_8_2_reattach_fixed (reattach work in progress)
→ M15_reattach_patch

### ups_hid_parser.c lineage
v14_3 → v14_3_1 → v14_4_output_voltage_probe

### ups_hid_descriptor_map lineage
v14_5

### nut_server lineage
v14_2_metadata

## Notes
- CURRENT baseline source files belong in src/ root, not here
- When a new confirmed anchor is reached, archive the superseded files here
  and update REVERT-INDEX.md
