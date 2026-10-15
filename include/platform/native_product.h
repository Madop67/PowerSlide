#ifndef NATIVE_PRODUCT_H
#define NATIVE_PRODUCT_H

// User-facing product name (window title, version banner, menus). Code, file
// and target names keep the upstream ctr-native naming to ease merges.
#define NATIVE_PRODUCT_NAME "PowerSlide"

// Written to the base directory by the menu's "Change disc image…" button; the
// next launch of a packaged build deletes it and runs disc setup again.
#define NATIVE_SETUP_REQUEST_FILE "setup-requested"

#endif
