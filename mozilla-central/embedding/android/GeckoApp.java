/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import java.io.*;
import java.util.*;
import java.util.zip.*;
import java.nio.*;
import java.nio.channels.FileChannel;
import java.util.concurrent.*;
import java.lang.reflect.*;

import android.os.*;
import android.app.*;
import android.text.*;
import android.view.*;
import android.view.inputmethod.*;
import android.content.*;
import android.content.res.*;
import android.graphics.*;
import android.widget.*;
import android.hardware.*;

import android.util.*;
import android.net.*;
import android.database.*;
import android.provider.*;
import android.content.pm.*;
import android.content.pm.PackageManager.*;
import dalvik.system.*;

abstract public class GeckoApp
    extends Activity
{
    private static final String LOG_FILE_NAME     = "GeckoApp";

    public static final String ACTION_ALERT_CLICK = "org.mozilla.gecko.ACTION_ALERT_CLICK";
    public static final String ACTION_ALERT_CLEAR = "org.mozilla.gecko.ACTION_ALERT_CLEAR";
    public static final String ACTION_WEBAPP      = "org.mozilla.gecko.WEBAPP";
    public static final String ACTION_DEBUG       = "org.mozilla.gecko.DEBUG";
    public static final String ACTION_BOOKMARK    = "org.mozilla.gecko.BOOKMARK";

    public static AbsoluteLayout mainLayout;
    public static GeckoSurfaceView surfaceView;
    public static SurfaceView cameraView;
    public static GeckoApp mAppContext;
    public static boolean mFullscreen = false;
    public static File sGREDir = null;
    static Thread mLibLoadThread = null;
    public Handler mMainHandler;
    private IntentFilter mConnectivityFilter;
    private BroadcastReceiver mConnectivityReceiver;
    private BroadcastReceiver mBatteryReceiver;

    enum LaunchState {PreLaunch, Launching, WaitForDebugger,
                      Launched, GeckoRunning, GeckoExiting};
    private static LaunchState sLaunchState = LaunchState.PreLaunch;
    private static boolean sTryCatchAttached = false;


    static boolean checkLaunchState(LaunchState checkState) {
        synchronized(sLaunchState) {
            return sLaunchState == checkState;
        }
    }

    static void setLaunchState(LaunchState setState) {
        synchronized(sLaunchState) {
            sLaunchState = setState;
        }
    }

    // if mLaunchState is equal to checkState this sets mLaunchState to setState
    // and return true. Otherwise we return false.
    static boolean checkAndSetLaunchState(LaunchState checkState, LaunchState setState) {
        synchronized(sLaunchState) {
            if (sLaunchState != checkState)
                return false;
            sLaunchState = setState;
            return true;
        }
    }

    void showErrorDialog(String message)
    {
        new AlertDialog.Builder(this)
            .setMessage(message)
            .setCancelable(false)
            .setPositiveButton(R.string.exit_label,
                               new DialogInterface.OnClickListener() {
                                   public void onClick(DialogInterface dialog,
                                                       int id)
                                   {
                                       GeckoApp.this.finish();
                                       System.exit(0);
                                   }
                               }).show();
    }

    public static final String PLUGIN_ACTION = "android.webkit.PLUGIN";

    /**
     * A plugin that wish to be loaded in the WebView must provide this permission
     * in their AndroidManifest.xml.
     */
    public static final String PLUGIN_PERMISSION = "android.webkit.permission.PLUGIN";

    private static final String LOGTAG = "PluginManager";

    private static final String PLUGIN_SYSTEM_LIB = "/system/lib/plugins/";

    private static final String PLUGIN_TYPE = "type";
    private static final String TYPE_NATIVE = "native";
    public ArrayList<PackageInfo> mPackageInfoCache = new ArrayList<PackageInfo>();

    String[] getPluginDirectories() {

        ArrayList<String> directories = new ArrayList<String>();
        PackageManager pm = this.mAppContext.getPackageManager();
        List<ResolveInfo> plugins = pm.queryIntentServices(new Intent(PLUGIN_ACTION),
                PackageManager.GET_SERVICES | PackageManager.GET_META_DATA);

        synchronized(mPackageInfoCache) {

            // clear the list of existing packageInfo objects
            mPackageInfoCache.clear();


            for (ResolveInfo info : plugins) {

                // retrieve the plugin's service information
                ServiceInfo serviceInfo = info.serviceInfo;
                if (serviceInfo == null) {
                    Log.w(LOGTAG, "Ignore bad plugin");
                    continue;
                }

                Log.w(LOGTAG, "Loading plugin: " + serviceInfo.packageName);


                // retrieve information from the plugin's manifest
                PackageInfo pkgInfo;
                try {
                    pkgInfo = pm.getPackageInfo(serviceInfo.packageName,
                                    PackageManager.GET_PERMISSIONS
                                    | PackageManager.GET_SIGNATURES);
                } catch (Exception e) {
                    Log.w(LOGTAG, "Can't find plugin: " + serviceInfo.packageName);
                    continue;
                }
                if (pkgInfo == null) {
                    Log.w(LOGTAG, "Loading plugin: " + serviceInfo.packageName + ". Could not load package information.");
                    continue;
                }

                /*
                 * find the location of the plugin's shared library. The default
                 * is to assume the app is either a user installed app or an
                 * updated system app. In both of these cases the library is
                 * stored in the app's data directory.
                 */
                String directory = pkgInfo.applicationInfo.dataDir + "/lib";
                final int appFlags = pkgInfo.applicationInfo.flags;
                final int updatedSystemFlags = ApplicationInfo.FLAG_SYSTEM |
                                               ApplicationInfo.FLAG_UPDATED_SYSTEM_APP;
                // preloaded system app with no user updates
                if ((appFlags & updatedSystemFlags) == ApplicationInfo.FLAG_SYSTEM) {
                    directory = PLUGIN_SYSTEM_LIB + pkgInfo.packageName;
                }

                // check if the plugin has the required permissions
                String permissions[] = pkgInfo.requestedPermissions;
                if (permissions == null) {
                    Log.w(LOGTAG, "Loading plugin: " + serviceInfo.packageName + ". Does not have required permission.");
                    continue;
                }
                boolean permissionOk = false;
                for (String permit : permissions) {
                    if (PLUGIN_PERMISSION.equals(permit)) {
                        permissionOk = true;
                        break;
                    }
                }
                if (!permissionOk) {
                    Log.w(LOGTAG, "Loading plugin: " + serviceInfo.packageName + ". Does not have required permission (2).");
                    continue;
                }

                // check to ensure the plugin is properly signed
                Signature signatures[] = pkgInfo.signatures;
                if (signatures == null) {
                    Log.w(LOGTAG, "Loading plugin: " + serviceInfo.packageName + ". Not signed.");
                    continue;
                }

                // determine the type of plugin from the manifest
                if (serviceInfo.metaData == null) {
                    Log.e(LOGTAG, "The plugin '" + serviceInfo.name + "' has no type defined");
                    continue;
                }

                String pluginType = serviceInfo.metaData.getString(PLUGIN_TYPE);
                if (!TYPE_NATIVE.equals(pluginType)) {
                    Log.e(LOGTAG, "Unrecognized plugin type: " + pluginType);
                    continue;
                }

                try {
                    Class<?> cls = getPluginClass(serviceInfo.packageName, serviceInfo.name);

                    //TODO implement any requirements of the plugin class here!
                    boolean classFound = true;

                    if (!classFound) {
                        Log.e(LOGTAG, "The plugin's class' " + serviceInfo.name + "' does not extend the appropriate class.");
                        continue;
                    }

                } catch (NameNotFoundException e) {
                    Log.e(LOGTAG, "Can't find plugin: " + serviceInfo.packageName);
                    continue;
                } catch (ClassNotFoundException e) {
                    Log.e(LOGTAG, "Can't find plugin's class: " + serviceInfo.name);
                    continue;
                }

                // if all checks have passed then make the plugin available
                mPackageInfoCache.add(pkgInfo);
                directories.add(directory);
            }
        }

        return directories.toArray(new String[directories.size()]);
    }

    Class<?> getPluginClass(String packageName, String className)
            throws NameNotFoundException, ClassNotFoundException {
        Context pluginContext = this.mAppContext.createPackageContext(packageName,
                Context.CONTEXT_INCLUDE_CODE |
                Context.CONTEXT_IGNORE_SECURITY);
        ClassLoader pluginCL = pluginContext.getClassLoader();
        return pluginCL.loadClass(className);
    }

    // Returns true when the intent is going to be handled by gecko launch
    boolean launch(Intent intent)
    {
        if (!checkAndSetLaunchState(LaunchState.Launching, LaunchState.Launched))
            return false;

        if (intent == null)
            intent = getIntent();
        final Intent i = intent;
        new Thread() {
            public void run() {
                try {
                    if (mLibLoadThread != null)
                        mLibLoadThread.join();
                } catch (InterruptedException ie) {}

                // Show the URL we are about to load, if the intent has one
                if (Intent.ACTION_VIEW.equals(i.getAction())) {
                    surfaceView.mSplashURL = i.getDataString();
                }
                surfaceView.drawSplashScreen();

                // unpack files in the components directory
                try {
                    unpackComponents();
                } catch (FileNotFoundException fnfe) {
                    Log.e(LOG_FILE_NAME, "error unpacking components", fnfe);
                    Looper.prepare();
                    showErrorDialog(getString(R.string.error_loading_file));
                    Looper.loop();
                    return;
                } catch (IOException ie) {
                    Log.e(LOG_FILE_NAME, "error unpacking components", ie);
                    String msg = ie.getMessage();
                    Looper.prepare();
                    if (msg != null && msg.equalsIgnoreCase("No space left on device"))
                        showErrorDialog(getString(R.string.no_space_to_start_error));
                    else
                        showErrorDialog(getString(R.string.error_loading_file));
                    Looper.loop();
                    return;
                }

                // and then fire us up
                try {
                    String env = i.getStringExtra("env0");
                    GeckoAppShell.runGecko(getApplication().getPackageResourcePath(),
                                           i.getStringExtra("args"),
                                           i.getDataString());
                } catch (Exception e) {
                    Log.e(LOG_FILE_NAME, "top level exception", e);
                    StringWriter sw = new StringWriter();
                    PrintWriter pw = new PrintWriter(sw);
                    e.printStackTrace(pw);
                    pw.flush();
                    GeckoAppShell.reportJavaCrash(sw.toString());
                }
            }
        }.start();
        return true;
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        mAppContext = this;
        mMainHandler = new Handler();

        if (!sTryCatchAttached) {
            sTryCatchAttached = true;
            mMainHandler.post(new Runnable() {
                public void run() {
                    try {
                        Looper.loop();
                    } catch (Exception e) {
                        Log.e(LOG_FILE_NAME, "top level exception", e);
                        StringWriter sw = new StringWriter();
                        PrintWriter pw = new PrintWriter(sw);
                        e.printStackTrace(pw);
                        pw.flush();
                        GeckoAppShell.reportJavaCrash(sw.toString());
                    }
                    // resetting this is kinda pointless, but oh well
                    sTryCatchAttached = false;
                }
            });
        }

        SharedPreferences settings = getPreferences(Activity.MODE_PRIVATE);
        String localeCode = settings.getString(getPackageName() + ".locale", "");
        if (localeCode != null && localeCode.length() > 0)
            GeckoAppShell.setSelectedLocale(localeCode);

        Log.i(LOG_FILE_NAME, "create");
        super.onCreate(savedInstanceState);

        if (sGREDir == null)
            sGREDir = new File(this.getApplicationInfo().dataDir);

        getWindow().setFlags(mFullscreen ?
                             WindowManager.LayoutParams.FLAG_FULLSCREEN : 0,
                             WindowManager.LayoutParams.FLAG_FULLSCREEN);

        if (cameraView == null) {
            cameraView = new SurfaceView(this);
            cameraView.getHolder().setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
        }

        if (surfaceView == null)
            surfaceView = new GeckoSurfaceView(this);
        else
            mainLayout.removeAllViews();

        mainLayout = new AbsoluteLayout(this);
        mainLayout.addView(surfaceView,
                           new AbsoluteLayout.LayoutParams(AbsoluteLayout.LayoutParams.MATCH_PARENT, // level 8
                                                           AbsoluteLayout.LayoutParams.MATCH_PARENT,
                                                           0,
                                                           0));

        setContentView(mainLayout,
                       new ViewGroup.LayoutParams(ViewGroup.LayoutParams.FILL_PARENT,
                                                  ViewGroup.LayoutParams.FILL_PARENT));

        mConnectivityFilter = new IntentFilter();
        mConnectivityFilter.addAction(ConnectivityManager.CONNECTIVITY_ACTION);
        mConnectivityReceiver = new GeckoConnectivityReceiver();

        IntentFilter batteryFilter = new IntentFilter();
        batteryFilter.addAction(Intent.ACTION_BATTERY_CHANGED);
        mBatteryReceiver = new GeckoBatteryManager();
        registerReceiver(mBatteryReceiver, batteryFilter);

        if (SmsManager.getInstance() != null) {
            SmsManager.getInstance().start();
        }

        GeckoNetworkManager.getInstance().init();

        if (!checkAndSetLaunchState(LaunchState.PreLaunch,
                                    LaunchState.Launching))
            return;

        checkAndLaunchUpdate();
        mLibLoadThread = new Thread(new Runnable() {
            public void run() {
                // At some point while loading the gecko libs our default locale gets set
                // so just save it to locale here and reset it as default after the join
                Locale locale = Locale.getDefault();
                GeckoAppShell.loadGeckoLibs(
                    getApplication().getPackageResourcePath());
                Locale.setDefault(locale);
                Resources res = getBaseContext().getResources();
                Configuration config = res.getConfiguration();
                config.locale = locale;
                res.updateConfiguration(config, res.getDisplayMetrics());
            }});
        mLibLoadThread.start();
    }

    public void enableCameraView() {
        // Some phones (eg. nexus S) need at least a 8x16 preview size
        mainLayout.addView(cameraView, new AbsoluteLayout.LayoutParams(8, 16, 0, 0));
    }

    public void disableCameraView() {
        mainLayout.removeView(cameraView);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        if (checkLaunchState(LaunchState.GeckoExiting)) {
            // We're exiting and shouldn't try to do anything else just incase
            // we're hung for some reason we'll force the process to exit
            System.exit(0);
            return;
        }
        final String action = intent.getAction();
        if (ACTION_DEBUG.equals(action) &&
            checkAndSetLaunchState(LaunchState.Launching, LaunchState.WaitForDebugger)) {

            mMainHandler.postDelayed(new Runnable() {
                public void run() {
                    Log.i(LOG_FILE_NAME, "Launching from debug intent after 5s wait");
                    setLaunchState(LaunchState.Launching);
                    launch(null);
                }
            }, 1000 * 5 /* 5 seconds */);
            Log.i(LOG_FILE_NAME, "Intent : ACTION_DEBUG - waiting 5s before launching");
            return;
        }
        if (checkLaunchState(LaunchState.WaitForDebugger) || launch(intent))
            return;

        if (Intent.ACTION_MAIN.equals(action)) {
            Log.i(LOG_FILE_NAME, "Intent : ACTION_MAIN");
            GeckoAppShell.sendEventToGecko(new GeckoEvent(""));
        }
        else if (Intent.ACTION_VIEW.equals(action)) {
            String uri = intent.getDataString();
            GeckoAppShell.sendEventToGecko(new GeckoEvent(uri));
            Log.i(LOG_FILE_NAME,"onNewIntent: "+uri);
        }
        else if (ACTION_WEBAPP.equals(action)) {
            String uri = intent.getStringExtra("args");
            GeckoAppShell.sendEventToGecko(new GeckoEvent(uri));
            Log.i(LOG_FILE_NAME,"Intent : WEBAPP - " + uri);
        }
        else if (ACTION_BOOKMARK.equals(action)) {
            String args = intent.getStringExtra("args");
            GeckoAppShell.sendEventToGecko(new GeckoEvent(args));
            Log.i(LOG_FILE_NAME,"Intent : BOOKMARK - " + args);
        }
    }

    @Override
    public void onPause()
    {
        Log.i(LOG_FILE_NAME, "pause");
        GeckoAppShell.sendEventToGecko(new GeckoEvent(GeckoEvent.ACTIVITY_PAUSING));
        // The user is navigating away from this activity, but nothing
        // has come to the foreground yet; for Gecko, we may want to
        // stop repainting, for example.

        // Whatever we do here should be fast, because we're blocking
        // the next activity from showing up until we finish.

        // onPause will be followed by either onResume or onStop.
        super.onPause();

        unregisterReceiver(mConnectivityReceiver);
        GeckoNetworkManager.getInstance().stop();
        GeckoScreenOrientationListener.getInstance().stop();
    }

    @Override
    public void onResume()
    {
        Log.i(LOG_FILE_NAME, "resume");
        if (checkLaunchState(LaunchState.GeckoRunning))
            GeckoAppShell.onResume();
        // After an onPause, the activity is back in the foreground.
        // Undo whatever we did in onPause.
        super.onResume();

        // Just in case. Normally we start in onNewIntent
        if (checkLaunchState(LaunchState.PreLaunch) ||
            checkLaunchState(LaunchState.Launching))
            onNewIntent(getIntent());

        registerReceiver(mConnectivityReceiver, mConnectivityFilter);
        GeckoNetworkManager.getInstance().start();
        GeckoScreenOrientationListener.getInstance().start();
    }

    @Override
    public void onStop()
    {
        Log.i(LOG_FILE_NAME, "stop");
        // We're about to be stopped, potentially in preparation for
        // being destroyed.  We're killable after this point -- as I
        // understand it, in extreme cases the process can be terminated
        // without going through onDestroy.
        //
        // We might also get an onRestart after this; not sure what
        // that would mean for Gecko if we were to kill it here.
        // Instead, what we should do here is save prefs, session,
        // etc., and generally mark the profile as 'clean', and then
        // dirty it again if we get an onResume.

        GeckoAppShell.sendEventToGecko(new GeckoEvent(GeckoEvent.ACTIVITY_STOPPING));
        super.onStop();
        GeckoAppShell.putChildInBackground();
    }

    @Override
    public void onRestart()
    {
        Log.i(LOG_FILE_NAME, "restart");
        GeckoAppShell.putChildInForeground();
        super.onRestart();
    }

    @Override
    public void onStart()
    {
        Log.i(LOG_FILE_NAME, "start");
        GeckoAppShell.sendEventToGecko(new GeckoEvent(GeckoEvent.ACTIVITY_START));
        super.onStart();
    }

    @Override
    public void onDestroy()
    {
        Log.i(LOG_FILE_NAME, "destroy");

        // Tell Gecko to shutting down; we'll end up calling System.exit()
        // in onXreExit.
        if (isFinishing())
            GeckoAppShell.sendEventToGecko(new GeckoEvent(GeckoEvent.ACTIVITY_SHUTDOWN));

        if (SmsManager.getInstance() != null) {
            SmsManager.getInstance().stop();
            if (isFinishing())
                SmsManager.getInstance().shutdown();
        }

        GeckoNetworkManager.getInstance().stop();
        GeckoScreenOrientationListener.getInstance().stop();

        super.onDestroy();

        unregisterReceiver(mBatteryReceiver);
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration newConfig)
    {
        Log.i(LOG_FILE_NAME, "configuration changed");
        // nothing, just ignore
        super.onConfigurationChanged(newConfig);
    }

    @Override
    public void onLowMemory()
    {
        Log.e(LOG_FILE_NAME, "low memory");
        if (checkLaunchState(LaunchState.GeckoRunning))
            GeckoAppShell.onLowMemory();
        super.onLowMemory();
    }

    abstract public String getPackageName();
    abstract public String getContentProcessName();

    protected void unpackComponents()
        throws IOException, FileNotFoundException
    {
        File applicationPackage = new File(getApplication().getPackageResourcePath());
        File componentsDir = new File(sGREDir, "components");
        if (componentsDir.lastModified() == applicationPackage.lastModified())
            return;

        componentsDir.mkdir();
        componentsDir.setLastModified(applicationPackage.lastModified());

        GeckoAppShell.killAnyZombies();

        ZipFile zip = new ZipFile(applicationPackage);

        byte[] buf = new byte[32768];
        try {
            if (unpackFile(zip, buf, null, "removed-files"))
                removeFiles();
        } catch (Exception ex) {
            // This file may not be there, so just log any errors and move on
            Log.w(LOG_FILE_NAME, "error removing files", ex);
        }

        // copy any .xpi file into an extensions/ directory
        Enumeration<? extends ZipEntry> zipEntries = zip.entries();
        while (zipEntries.hasMoreElements()) {
            ZipEntry entry = zipEntries.nextElement();
            if (entry.getName().startsWith("extensions/") && entry.getName().endsWith(".xpi")) {
                Log.i("GeckoAppJava", "installing extension : " + entry.getName());
                unpackFile(zip, buf, entry, entry.getName());
            }
        }
    }

    void removeFiles() throws IOException {
        BufferedReader reader = new BufferedReader(
            new FileReader(new File(sGREDir, "removed-files")));
        try {
            for (String removedFileName = reader.readLine(); 
                 removedFileName != null; removedFileName = reader.readLine()) {
                File removedFile = new File(sGREDir, removedFileName);
                if (removedFile.exists())
                    removedFile.delete();
            }
        } finally {
            reader.close();
        }
        
    }

    private boolean unpackFile(ZipFile zip, byte[] buf, ZipEntry fileEntry,
                            String name)
        throws IOException, FileNotFoundException
    {
        if (fileEntry == null)
            fileEntry = zip.getEntry(name);
        if (fileEntry == null)
            throw new FileNotFoundException("Can't find " + name + " in " +
                                            zip.getName());

        File outFile = new File(sGREDir, name);
        if (outFile.lastModified() == fileEntry.getTime() &&
            outFile.length() == fileEntry.getSize())
            return false;

        File dir = outFile.getParentFile();
        if (!dir.exists())
            dir.mkdirs();

        InputStream fileStream;
        fileStream = zip.getInputStream(fileEntry);

        OutputStream outStream = new FileOutputStream(outFile);

        while (fileStream.available() > 0) {
            int read = fileStream.read(buf, 0, buf.length);
            outStream.write(buf, 0, read);
        }

        fileStream.close();
        outStream.close();
        outFile.setLastModified(fileEntry.getTime());
        return true;
    }

    public void addEnvToIntent(Intent intent) {
        Map<String,String> envMap = System.getenv();
        Set<Map.Entry<String,String>> envSet = envMap.entrySet();
        Iterator<Map.Entry<String,String>> envIter = envSet.iterator();
        int c = 0;
        while (envIter.hasNext()) {
            Map.Entry<String,String> entry = envIter.next();
            intent.putExtra("env" + c, entry.getKey() + "="
                            + entry.getValue());
            c++;
        }
    }

    public void doRestart() {
        try {
            String action = "org.mozilla.gecko.restart";
            Intent intent = new Intent(action);
            intent.setClassName(getPackageName(),
                                getPackageName() + ".Restarter");
            addEnvToIntent(intent);
            intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK |
                            Intent.FLAG_ACTIVITY_MULTIPLE_TASK);
            Log.i(LOG_FILE_NAME, intent.toString());
            GeckoAppShell.killAnyZombies();
            startActivity(intent);
        } catch (Exception e) {
            Log.i(LOG_FILE_NAME, "error doing restart", e);
        }
        finish();
        // Give the restart process time to start before we die
        GeckoAppShell.waitForAnotherGeckoProc();
    }

    public void handleNotification(String action, String alertName, String alertCookie) {
        GeckoAppShell.handleNotification(action, alertName, alertCookie);
    }

    private void checkAndLaunchUpdate() {
        Log.i(LOG_FILE_NAME, "Checking for an update");

        int statusCode = 8; // UNEXPECTED_ERROR
        File baseUpdateDir = null;
        if (Build.VERSION.SDK_INT >= 8)
            baseUpdateDir = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
        else
            baseUpdateDir = new File(Environment.getExternalStorageDirectory().getPath(), "download");

        File updateDir = new File(new File(baseUpdateDir, "updates"),"0");

        File updateFile = new File(updateDir, "update.apk");
        File statusFile = new File(updateDir, "update.status");

        if (!statusFile.exists() || !readUpdateStatus(statusFile).equals("pending"))
            return;

        if (!updateFile.exists())
            return;

        Log.i(LOG_FILE_NAME, "Update is available!");

        // Launch APK
        File updateFileToRun = new File(updateDir, getPackageName() + "-update.apk");
        try {
            if (updateFile.renameTo(updateFileToRun)) {
                String amCmd = "/system/bin/am start -a android.intent.action.VIEW " +
                               "-n com.android.packageinstaller/.PackageInstallerActivity -d file://" +
                               updateFileToRun.getPath();
                Log.i(LOG_FILE_NAME, amCmd);
                Runtime.getRuntime().exec(amCmd);
                statusCode = 0; // OK
            } else {
                Log.i(LOG_FILE_NAME, "Cannot rename the update file!");
                statusCode = 7; // WRITE_ERROR
            }
        } catch (Exception e) {
            Log.i(LOG_FILE_NAME, "error launching installer to update", e);
        }

        // Update the status file
        String status = statusCode == 0 ? "succeeded\n" : "failed: "+ statusCode + "\n";

        OutputStream outStream;
        try {
            byte[] buf = status.getBytes("UTF-8");
            outStream = new FileOutputStream(statusFile);
            outStream.write(buf, 0, buf.length);
            outStream.close();
        } catch (Exception e) {
            Log.i(LOG_FILE_NAME, "error writing status file", e);
        }

        if (statusCode == 0)
            System.exit(0);
    }

    private String readUpdateStatus(File statusFile) {
        String status = "";
        try {
            BufferedReader reader = new BufferedReader(new FileReader(statusFile));
            status = reader.readLine();
            reader.close();
        } catch (Exception e) {
            Log.i(LOG_FILE_NAME, "error reading update status", e);
        }
        return status;
    }

    static final int FILE_PICKER_REQUEST = 1;

    private SynchronousQueue<String> mFilePickerResult = new SynchronousQueue();
    public String showFilePicker(String aMimeType) {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(aMimeType);
        GeckoApp.this.
            startActivityForResult(
                Intent.createChooser(intent, getString(R.string.choose_file)),
                FILE_PICKER_REQUEST);
        String filePickerResult = "";

        try {
            while (null == (filePickerResult = mFilePickerResult.poll(1, TimeUnit.MILLISECONDS))) {
                Log.i("GeckoApp", "processing events from showFilePicker ");
                GeckoAppShell.processNextNativeEvent();
            }
        } catch (InterruptedException e) {
            Log.i(LOG_FILE_NAME, "showing file picker ",  e);
        }

        return filePickerResult;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode,
                                    Intent data) {
        String filePickerResult = "";
        if (data != null && resultCode == RESULT_OK) {
            try {
                ContentResolver cr = getContentResolver();
                Uri uri = data.getData();
                Cursor cursor = GeckoApp.mAppContext.getContentResolver().query(
                    uri, 
                    new String[] { OpenableColumns.DISPLAY_NAME },
                    null, 
                    null, 
                    null);
                String name = null;
                if (cursor != null) {
                    try {
                        if (cursor.moveToNext()) {
                            name = cursor.getString(0);
                        }
                    } finally {
                        cursor.close();
                    }
                }
                String fileName = "tmp_";
                String fileExt = null;
                int period;
                if (name == null || (period = name.lastIndexOf('.')) == -1) {
                    String mimeType = cr.getType(uri);
                    fileExt = "." + GeckoAppShell.getExtensionFromMimeType(mimeType);
                } else {
                    fileExt = name.substring(period);
                    fileName = name.substring(0, period);
                }
                File file = File.createTempFile(fileName, fileExt, sGREDir);

                FileOutputStream fos = new FileOutputStream(file);
                InputStream is = cr.openInputStream(uri);
                byte[] buf = new byte[4096];
                int len = is.read(buf);
                while (len != -1) {
                    fos.write(buf, 0, len);
                    len = is.read(buf);
                }
                fos.close();
                filePickerResult =  file.getAbsolutePath();
            }catch (Exception e) {
                Log.e(LOG_FILE_NAME, "showing file picker", e);
            }
        }
        try {
            mFilePickerResult.put(filePickerResult);
        } catch (InterruptedException e) {
            Log.i(LOG_FILE_NAME, "error returning file picker result", e);
        }
    }
}
