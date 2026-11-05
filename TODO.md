# Whisker Menu Aero — progress

## Done

- [x] Import pristine upstream xfce4-whiskermenu-plugin 2.10.0 as the first commit.
- [x] Three-column layout: 77 px action strip, 409 px letter-grouped app list,
      stock pinned-favorites grid in the remaining width.
- [x] Bottom-anchored strip buttons: Videos, Music, Documents, Settings, Power.
- [x] All Applications list built from `ApplicationsPage::find_all()`, grouped
      by the plain display name's first letter (fixed: the markup text starts
      with an invisible LRM, which put everything under `#`), with `#` for
      non-alphabetic names.
- [x] Clicking an app row adds it to Recent, hides the menu, and launches it.
- [x] Configurable application row spacing, default 20 px
      (`/app-row-spacing` plus a settings-dialog spin button).
- [x] Old title row, search box, category sidebar and horizontal category row
      hidden; keyboard focus goes to the app list.
- [x] Custom black background drawn by the plugin, with configurable
      `/background-alpha` (double, default 0.8).
- [x] White text via an application-priority CSS provider.
- [x] Settings dialog opacity slider repointed to `background_alpha` (0.00–1.00).
- [x] Podman build (`build.sh`, `Containerfile`) and user-local install
      (`install.sh`) with no root required.
- [x] Verified live: user-local `.so` loaded by the panel, 0.8 background,
      pure white text, three columns visible, list populated and scrollable.

## Remaining / optional

- [ ] Remove or disable upstream layout options in the settings dialog that no
      longer affect the fixed three-column layout.
- [ ] Add the upstream right-click context menu to the custom application list
      (Add to Favorites / Desktop / Panel / Autostart, edit, hide).
- [ ] Decide whether to reintroduce search (e.g. type-to-filter in the app
      list) or keep the menu browse-only.
- [ ] Optional visual polish: separate tint for the left strip, hover states
      for the letter headers, scrollbar styling.

## Next desktop mod

- [ ] Aero-style fullscreen workspace picker/switcher.
