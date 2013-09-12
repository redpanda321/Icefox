Components.utils.import("resource://gre/modules/Services.jsm");

function install(data, reason) {
  Components.utils.import(data.resourceURI.spec + "version.jsm");
  Services.prefs.setIntPref("bootstraptest.installed_version", VERSION);
  Services.prefs.setIntPref("bootstraptest.install_reason", reason);
  Components.utils.unload(data.resourceURI.spec + "version.jsm");
}

function startup(data, reason) {
  Components.utils.import(data.resourceURI.spec + "version.jsm");
  Services.prefs.setIntPref("bootstraptest.active_version", VERSION);
  Services.prefs.setIntPref("bootstraptest.startup_reason", reason);
  Components.utils.unload(data.resourceURI.spec + "version.jsm");
}

function shutdown(data, reason) {
  Services.prefs.setIntPref("bootstraptest.active_version", 0);
  Services.prefs.setIntPref("bootstraptest.shutdown_reason", reason);
}

function uninstall(data, reason) {
  Services.prefs.setIntPref("bootstraptest.installed_version", 0);
  Services.prefs.setIntPref("bootstraptest.uninstall_reason", reason);
}
