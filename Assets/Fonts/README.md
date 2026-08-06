# AFTERIMAGE typography

AFTERIMAGE does **not** embed proprietary or commercial font files.

UI text uses the host system geometric / neo-grotesque sans via
`AfterimageFonts::preferredTypefaceName()`:

| Platform | Preferred face |
|----------|----------------|
| macOS / iOS | Avenir Next |
| Windows | Segoe UI |
| Other | Sans-Serif (JUCE fallback) |

These faces are licensed with the operating system. Redistribution of this
plugin does not ship font binaries.
