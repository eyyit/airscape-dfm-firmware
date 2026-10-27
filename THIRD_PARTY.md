# Third-party inventory and publication gate

Original project C, Python and JavaScript are independent implementations;
manufacturer firmware, extracted binaries and decompiler output are excluded.
AirScape, Seeed and Sensirion names identify compatible hardware only.

The pinned ESP-IDF SDK is external to this repository. It contains components
with their own licensing requirements, including cJSON and cryptography.
Tests retrieve cJSON from that SDK into ignored artifacts; it is not vendored.
GitHub Actions, Docker images and host build tools are external dependencies.

Before publication/distribution, select a license for original code, review
upstream license files at the exact locked SDK revision, and include all notices
required for any distributed binaries. This inventory is not legal clearance.
