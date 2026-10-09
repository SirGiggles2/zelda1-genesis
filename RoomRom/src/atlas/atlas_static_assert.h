/* RoomRom atlas: portable static assertion macro.
 *
 * Phase 0 of the atlas north-star spec (2026-05-01-roomrom-z1-full-atlas-
 * design.md, section 6.4) probes the SGDK m68k gcc toolchain for C11
 * _Static_assert support. If supported, atlas renderer-side asserts use
 * _Static_assert directly. If not, the macro falls back to a C89-compatible
 * typedef-array trick: a typedef of an array whose size is positive when
 * the predicate holds and -1 (illegal) when it fails -> compile error fires
 * at the typedef.
 *
 * Probe driver: tools/phase0_probe.bat compiled tools/phase0_static_assert_
 * probe.c against the SGDK toolchain on 2026-05-02 and the C11 path is
 * active. Re-run the probe if the toolchain changes (sgdk version bump,
 * new builder host, etc).
 *
 * Usage by future renderer-side dispatch macros (Phase 4 of the atlas spec):
 *   ATLAS_ASSERT_SIZE(name, w, h);
 * which expands to a static assert against the registry's dispatch struct.
 */

#ifndef ROOMROM_ATLAS_STATIC_ASSERT_H
#define ROOMROM_ATLAS_STATIC_ASSERT_H

/* C11 path: _Static_assert. Probe at tools/phase0_static_assert_probe.c
 * confirmed SGDK m68k gcc supports this. */
#define ATLAS_STATIC_ASSERT(predicate, msg) \
    _Static_assert(predicate, msg)

/* C89 fallback path (kept for reference; flip to it by undef'ing the
 * above and uncommenting if a future toolchain change breaks the C11
 * path). The fallback names the typedef so duplicate uses on the same
 * line do not collide.
 *
 * #define ATLAS_STATIC_ASSERT_C89_INNER(predicate, name) \
 *     typedef char atlas_static_assert_##name[(predicate) ? 1 : -1]
 * #define ATLAS_STATIC_ASSERT(predicate, msg) \
 *     ATLAS_STATIC_ASSERT_C89_INNER(predicate, __LINE__)
 */

#endif /* ROOMROM_ATLAS_STATIC_ASSERT_H */
