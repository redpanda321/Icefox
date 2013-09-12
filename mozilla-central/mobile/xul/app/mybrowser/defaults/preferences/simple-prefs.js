/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pref("ipc.channel.socket.host", "192.168.20.21");
pref("general.useragent.override", "Mozilla/5.0 (X11; Ubuntu; Linux i686; rv:13.0) Gecko/20100101 Firefox/13.0.1");

pref("toolkit.defaultChromeURI", "chrome://simple/content/simple.xul");
pref("general.useragent.extra.simple", "SimpleApp/0.1");
pref("layers.acceleration.disabled", true);
pref("layers.acceleration.force-enabled", false);

/* plugins */
pref("plugins.force.wmode", "opaque");
pref("dom.ipc.plugins.enabled", false);

/* password manager */
pref("signon.rememberSignons", true);
pref("signon.expireMasterPassword", false);
pref("signon.SignonFileName", "signons.txt");

pref("mozilla.widget.force-24bpp", true);
pref("mozilla.widget.use-buffer-pixmap", true);
//pref("mozilla.widget.disable-native-theme", true);
pref("browser.dom.window.dump.enabled", false);
