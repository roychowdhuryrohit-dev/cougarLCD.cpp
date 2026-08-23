# Security policy

Please report vulnerabilities privately through GitHub's **Report a
vulnerability** feature when available. Do not post device serials or raw logs
containing personal paths in public issues.

This project handles a physical USB device. Treat changes to HID writes,
length/checksum handling, media chunking, device selection, service privilege,
or installer elevation as security-sensitive. The Windows diagnostic target
must remain read-only.

