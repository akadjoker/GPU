# Documentation review register

The generated reference only renders declarations parsed from Doxygen XML. It
must not introduce new functions, overloads, enum values, default arguments, or
ownership rules. This register records statements that need an implementation
or API decision before they become normative documentation.

| Location | Review item | Current safe wording |
| --- | --- | --- |
| `api/device.md`, `api/backends.md` | `GLSurface`, `VulkanSurface`, and GLES surface callback contracts are backend headers, not public `gpu/include/gpu` declarations. | Describe them as internal adapters and mark their signatures `REVIEW`. |
| `api/resources.md` | Exact validation/packing semantics for `TextureDataLayout` are not specified in public comments. | Document only its three fields: `offset`, `bytesPerRow`, `rowsPerImage`. |
| Generated enum reference | Enumerators without an explicit initializer have no numeric contract in the headers. | List the exact source names and explicit initializers only; do not invent ordinal values. |
| `api/diagnostics.md` | `GPUError::message` is a raw pointer with no public lifetime contract. | Do not promise ownership or retention. |
| `api/commands.md` | Command ordering and barrier requirements vary by backend implementation. | Treat the sequence as a usage example, not a normative guarantee, until each method is annotated. |

When a review item is resolved, update the public header comments and this
register together. The header change is authoritative; guide text must then be
updated from the exact signature and documented behavior.
