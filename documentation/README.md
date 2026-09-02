# GPU documentation

This directory contains the human-written API guide and the source for the
generated reference. The public headers under `gpu/include/gpu/` remain the
source of truth.

## Build the references

Install Doxygen and Python 3, then configure the optional CMake target:

```sh
cmake -S gpu -B build-docs -DGPU_BUILD_DOCS=ON
cmake --build build-docs --target docs
```

To write the generated tree to a repository directory instead, set
`GPU_DOCS_OUTPUT_DIR`, for example
`-DGPU_DOCS_OUTPUT_DIR=$PWD/documentation`.

With the default configuration, the output is written to `build-docs/docs/`.
The generated reference is `<output>/generated/api-reference.md`. The same
API is also available as `<output>/generated/api-reference.rst`, while
Doxygen's navigable HTML site is written to `<output>/html/index.html`. Guide
pages are copied to `<output>/api/`.

The checked-in `public_api_manifest.txt` is a coverage gate. It contains only
names verified against the public headers; parameter lists and signatures are
rendered from Doxygen XML so that documentation cannot silently invent an API.

The HTML output is standalone and can be opened directly in a browser. The RST
output can be included in Sphinx or another reStructuredText documentation
site.

## Guide

- [Overview](api/overview.md)
- [Device lifecycle](api/device.md)
- [Resources](api/resources.md)
- [Commands](api/commands.md)
- [Types and capabilities](api/types-and-capabilities.md)
- [Diagnostics and profiling](api/diagnostics.md)
- [Backend support](api/backends.md)
- [Review register](API_REVIEW.md)

Generated files should not be edited manually. Update the public headers or
the guide pages, then regenerate the reference.
