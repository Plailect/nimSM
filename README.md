# nimSM

This is a nim save manager that uses PxiFS0 to backup, restore, or zero out the nim savegame. Requires arm11 kernel privileges, which it currently gets with svchax.

This can be installed as a CIA, but if one can install CIAs, an equally valid method would be to just set the required exheader bitmasks to access the savegame with FS:USER.

This app's intended use is as a `.3dsx` running under \*hax with arm11 kernel privileges.

Backups of the nim savegame are saved to `sdmc://nimsavegame.bin`

Zeroing out or deleting the savegame will cause it to be recreated by nim. Doing this before a downgrade will fix the "[soft brick](https://github.com/Plailect/sysDowngrader/issues/1)" that occurs for some devices.

In its current form, it cannot delete the file because nim keeps an active handle to its savegame at all times, but it can zero it out which works fine for our purposes (triggering a regeneration).

Note that because fs must be terminated to get a PxiFS0 handle, this app requires a reboot after running!

## Building

Run `make`; requires ctrulib, makerom, and bannertool.

## Credits

+ Plailect
+ neobrain
