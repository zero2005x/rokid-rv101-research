# Private Materials: Do Not Upload Directly to HITCON Public Fields

> 中文版：[PRIVATE_MATERIALS_NOT_FOR_UPLOAD.md](PRIVATE_MATERIALS_NOT_FOR_UPLOAD.md)

This file is a prominent reminder; it does not itself contain confidential data.

The following materials stay in the original `RV101_Disclosure/confidential/` or the research repo, and are not copied into this package:

- exploit source, compiled ELF, retry wrapper;
- hard-coded kernel offsets, KASLR base / slide;
- kernel addresses such as `init_cred`, `selinux_state`, task / cred / fops / mm;
- forged object layout, reclaim / heap grooming parameters, controlled-write details;
- complete raw run logs;
- partition dumps, device serial, host path, keys / secrets.

If HITCON or Rokid requests the PoC, first confirm: the designated recipient, the purpose, the retention / deletion rules, whether it will be published automatically by the platform, and the acceptable encrypted transfer method. Provide only the minimum data needed for reproduction.
