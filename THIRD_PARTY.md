# Third-party software policy

DEOS-owned source code is Apache-2.0.

The project intentionally reuses mature platform software rather than reimplementing drivers and kernels for branding reasons. Third-party components retain their original licenses and notices.

Expected platform dependencies include:

- Espressif ESP-IDF — Apache-2.0 and component-specific licenses;
- FreeRTOS as distributed with ESP-IDF — upstream license retained;
- LVGL for the future graphical shell — MIT;
- board/peripheral drivers selected for each target — their upstream licenses retained.

Before a third-party component is merged, its license must be recorded here or in generated dependency notices. Code copied from unrelated firmware projects must not be introduced without license review.
