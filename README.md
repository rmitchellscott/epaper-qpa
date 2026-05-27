# epaper-qpa

Fork of [reMarkable's epaper-qpa](https://github.com/reMarkable/epaper-qpa) with support for external USB and Bluetooth keyboards.

The stock plugin maps every keyboard through the Type Folio's custom keymap, which remaps keys like brackets, grave/tilde, and End into dead keys or modifiers that don't make sense on a standard keyboard. This fork detects the input device name at open time. Type Folio devices (`rM_Keyboard`, `rm_hwmon_keyboard`) use the stock keymap unchanged, while any other keyboard gets fixups applied:

- **End key** restored (stock maps it to a Meta modifier)
- **Brackets and braces** restored to standard US layout (stock maps them to dead keys)
- **Grave/tilde** restored (stock maps grave to a dead key)
- **Ctrl+Alt+Arrow** console switching removed

Type Folio behavior is completely unchanged.

## Installation

### Vellum

```sh
vellum install external-keyboard-qpa
```

This also installs `qt-plugin-path`, which adds the xovi service drop-in so xochitl can find the plugin.

### Manual

1. Build for aarch64 (requires Docker):

   ```sh
   ./build.sh
   ```

2. Copy to the device:

   ```sh
   scp build-aarch64/libepaper.so root@remarkable:/path/to/plugins/platforms/
   ```

3. Create `/home/root/xovi/services/xochitl.service/qt-plugin-path.conf`:

   ```ini
   [Service]
   Environment="QT_PLUGIN_PATH=/path/to/plugins:/usr/lib/plugin"
   ```

4. Restart xovi.

## Building

Requires Qt 6.8+ and cmake 3.25+.

```sh
cmake -S . -Bbuild
cmake --build build
```
