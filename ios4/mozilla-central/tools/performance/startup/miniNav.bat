@mozilla.exe -P "Default User" -chrome chrome://navigator/content/miniNav.xul
@mozilla.exe -P "Default User" -chrome chrome://navigator/content/miniNav.xul
@grep "Navigator Window visible now" "%NS_TIMELINE_LOG_FILE%"
@mozilla.exe -P "Default User" -chrome chrome://navigator/content/miniNav.xul
@grep "Navigator Window visible now" "%NS_TIMELINE_LOG_FILE%"
@mozilla.exe -P "Default User" -chrome chrome://navigator/content/miniNav.xul
@grep "Navigator Window visible now" "%NS_TIMELINE_LOG_FILE%"
@copy "%NS_TIMELINE_LOG_FILE%" miniNav.log
