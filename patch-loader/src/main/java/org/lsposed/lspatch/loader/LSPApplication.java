package org.lsposed.lspatch.loader;

import static org.lsposed.lspatch.share.Constants.CONFIG_ASSET_PATH;
import static org.lsposed.lspatch.share.Constants.ORIGINAL_APK_ASSET_PATH;
import static org.lsposed.lspatch.share.Constants.PROVIDER_DEX_ASSET_PATH;

import android.app.ActivityThread;
import android.app.LoadedApk;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.res.CompatibilityInfo;
import android.os.Build;
import android.os.RemoteException;
import android.system.Os;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;
import org.lsposed.lspatch.loader.util.FileUtils;
import org.lsposed.lspatch.loader.util.XLog;
import org.lsposed.lspatch.service.FixedLocalApplicationService;
import org.lsposed.lspatch.service.LocalApplicationService;
import org.lsposed.lspatch.service.RemoteApplicationService;
import org.lsposed.lspatch.share.PatchConfig;
import org.lsposed.lspd.core.Startup;
import org.lsposed.lspd.models.Module;
import org.lsposed.lspd.service.ILSPApplicationService;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.function.BiConsumer;
import java.util.zip.ZipFile;

import dalvik.system.DexFile;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import hidden.HiddenApiBridge;

/**
 * Created by Windysha
 */
@SuppressWarnings("unused")
public class LSPApplication {

    private static final String TAG = "OPatch";
    private static final int FIRST_APP_ZYGOTE_ISOLATED_UID = 90000;
    private static final int PER_USER_RANGE = 100000;

    private static ActivityThread activityThread;
    private static LoadedApk stubLoadedApk;
    //private static LoadedApk appLoadedApk;

    private static PatchConfig config;

    public static boolean isIsolated() {
        return (android.os.Process.myUid() % PER_USER_RANGE) >= FIRST_APP_ZYGOTE_ISOLATED_UID;
    }

    public static void onLoad() throws RemoteException, IOException {
        if (isIsolated()) {
            XLog.d(TAG, "Skip isolated process");
            return;
        }
        activityThread = ActivityThread.currentActivityThread();
        var context = createLoadedApkWithContext();
        if (context == null) {
            XLog.e(TAG, "Error when creating context");
            return;
        }

        Log.d(TAG, "Initialize service client");
        ILSPApplicationService service;
        if (config.useManager) {
            try {
                service = new RemoteApplicationService(context);
                List<Module> m = service.getLegacyModulesList();
                JSONArray moduleArr = new JSONArray();
                for (Module module : m) {
                    JSONObject moduleObj = new JSONObject();
                    moduleObj.put("path",module.apkPath);
                    moduleObj.put("packageName",module.packageName);
                    moduleArr.put(moduleObj);
                }
                SharedPreferences shared = context.getSharedPreferences("opatch", Context.MODE_PRIVATE);
                shared.edit().putString("modules",moduleArr.toString()).commit();
                Log.e(TAG, "Success update module scope");
            }catch (Exception e){
                Log.e(TAG, "Failed to connect to manager, fallback to fixed local service");
                service = new FixedLocalApplicationService(context);
            }

        } else {
            service = new LocalApplicationService(context);
        }

        disableProfile(context);
        Startup.initXposed(false, ActivityThread.currentProcessName(), context.getApplicationInfo().dataDir, service);
        Startup.bootstrapXposed();
        // WARN: Since it uses `XResource`, the following class should not be initialized
        // before forkPostCommon is invoke. Otherwise, you will get failure of XResources

        if (config.outputLog){
            XposedBridge.setLogPrinter(new XposedLogPrinter(0,"OPatch"));
        }
        Log.i(TAG, "Load modules");
        LSPLoader.initModules(stubLoadedApk);
        Log.i(TAG, "Modules initialized");

        switchAllClassLoader();
        SigBypass.doSigBypass(context, config.sigBypassLevel);

        Log.i(TAG, "LSPatch bootstrap completed");
    }

    private static Context createLoadedApkWithContext() {
        try {
            var timeStart = System.currentTimeMillis();
            var mBoundApplication = XposedHelpers.getObjectField(activityThread, "mBoundApplication");

            stubLoadedApk = (LoadedApk) XposedHelpers.getObjectField(mBoundApplication, "info");
            var appInfo = (ApplicationInfo) XposedHelpers.getObjectField(mBoundApplication, "appInfo");
            var compatInfo = (CompatibilityInfo) XposedHelpers.getObjectField(mBoundApplication, "compatInfo");
            var baseClassLoader = stubLoadedApk.getClassLoader();

            try (var is = baseClassLoader.getResourceAsStream(CONFIG_ASSET_PATH)) {
                BufferedReader streamReader = new BufferedReader(new InputStreamReader(is, StandardCharsets.UTF_8));
                StringBuilder responseStrBuilder = new StringBuilder();
                String inputStr;
                while ((inputStr = streamReader.readLine()) != null) {
                    responseStrBuilder.append(inputStr);
                }
                fromJsonString(responseStrBuilder.toString());
            } catch (IOException e) {
                Log.e(TAG, "Failed to load config file");
                return null;
            }
            Log.i(TAG, "Use manager: " + config.useManager);
            Log.i(TAG, "Signature bypass level: " + config.sigBypassLevel);

            Path originPath = Paths.get(appInfo.dataDir, "cache/opatch/origin/");
            Path cacheApkPath;
            try (ZipFile sourceFile = new ZipFile(appInfo.sourceDir)) {
                cacheApkPath = originPath.resolve(sourceFile.getEntry(ORIGINAL_APK_ASSET_PATH).getCrc() + ".apk");
            }
            String sourceFileaa = appInfo.sourceDir;

            appInfo.sourceDir = cacheApkPath.toString();
            appInfo.publicSourceDir = cacheApkPath.toString();
            appInfo.appComponentFactory = config.appComponentFactory;




            if (!Files.exists(cacheApkPath)) {
                Log.i(TAG, "Extract original apk");
                FileUtils.deleteFolderIfExists(originPath);
                Files.createDirectories(originPath);
                try (InputStream is = baseClassLoader.getResourceAsStream(ORIGINAL_APK_ASSET_PATH)) {
                    Files.copy(is, cacheApkPath);
                }
            }
            Path providerPath = null;
            if (config.injectProvider){
                try (ZipFile sourceFile = new ZipFile(sourceFileaa)) {
                    providerPath = Paths.get(appInfo.dataDir, "cache/opatch/origin/p_" + sourceFile.getEntry(ORIGINAL_APK_ASSET_PATH).getCrc()+".dex");
                    Files.deleteIfExists(providerPath);
                    try (InputStream is = baseClassLoader.getResourceAsStream(PROVIDER_DEX_ASSET_PATH)) {
                        Files.copy(is, providerPath);
                    }
                }catch (Exception e){
                    Log.e(TAG, "Failed to inject provider:" + Log.getStackTraceString(e));
                }

            }

            cacheApkPath.toFile().setWritable(false);



            if (config.injectProvider){
                ClassLoader loader = stubLoadedApk.getClassLoader();
                Object dexPathList = XposedHelpers.getObjectField(loader, "pathList");
                Object dexElements = XposedHelpers.getObjectField(dexPathList, "dexElements");
                int length = Array.getLength(dexElements);
                Object newElements = Array.newInstance(dexElements.getClass().getComponentType(), length + 1);
                System.arraycopy(dexElements, 0, newElements, 0, length);

                DexFile dexFile = new DexFile(providerPath.toString());
                Object element = XposedHelpers.newInstance(XposedHelpers.findClass("dalvik.system.DexPathList$Element",loader), new Class[]{
                        DexFile.class
                },dexFile);
                Array.set(newElements, length, element);
                XposedHelpers.setObjectField(dexPathList, "dexElements", newElements);
            }

            var context = (Context) XposedHelpers.callStaticMethod(Class.forName("android.app.ContextImpl"), "createAppContext", activityThread, stubLoadedApk);
            if (config.appComponentFactory != null) {
                try {
                    context.getClassLoader().loadClass(config.appComponentFactory);
                } catch (ClassNotFoundException e) { // This will happen on some strange shells like 360
                    Log.w(TAG, "Original AppComponentFactory not found: " + config.appComponentFactory);
                    appInfo.appComponentFactory = null;
                }
            }
            Log.i(TAG,"createLoadedApkWithContext cost: " + (System.currentTimeMillis() - timeStart) + "ms");

            SigBypass.replaceApplication(appInfo.packageName, appInfo.sourceDir, appInfo.publicSourceDir);
            File[] fs = context.getExternalMediaDirs();
            if (fs == null || fs.length == 0) {
                Log.e(TAG, "Failed to get external media dirs");
            } else {
                XposedLogPrinter.targetRoot = fs[0];
                fs[0].mkdirs();
            }

            return context;
        } catch (Throwable e) {
            Log.e(TAG, "createLoadedApk", e);
            return null;
        }
    }

    private static void fromJsonString(String s)throws Exception{
        JSONObject jsonObject = new JSONObject(s);
        config = new PatchConfig(
                jsonObject.optBoolean("useManager"),
                jsonObject.optBoolean("debuggable"),

                jsonObject.optBoolean("overrideVersionCode"),
                jsonObject.optInt("sigBypassLevel"),
                jsonObject.optString("originalSignature"),
                jsonObject.optString("appComponentFactory"),
                jsonObject.optBoolean("injectProvider"),
                jsonObject.optBoolean("outputLog")
        );

    }

    public static void disableProfile(Context context) {
        final ArrayList<String> codePaths = new ArrayList<>();
        var appInfo = context.getApplicationInfo();
        var pkgName = context.getPackageName();
        if (appInfo == null) return;
        if ((appInfo.flags & ApplicationInfo.FLAG_HAS_CODE) != 0) {
            codePaths.add(appInfo.sourceDir);
        }
        if (appInfo.splitSourceDirs != null) {
            Collections.addAll(codePaths, appInfo.splitSourceDirs);
        }

        if (codePaths.isEmpty()) {
            // If there are no code paths there's no need to setup a profile file and register with
            // the runtime,
            return;
        }

        var profileDir = HiddenApiBridge.Environment_getDataProfilesDePackageDirectory(appInfo.uid / PER_USER_RANGE, pkgName);

        var attrs = PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("r--------"));

        for (int i = codePaths.size() - 1; i >= 0; i--) {
            String splitName = i == 0 ? null : appInfo.splitNames[i - 1];
            File curProfileFile = new File(profileDir, splitName == null ? "primary.prof" : splitName + ".split.prof").getAbsoluteFile();
            Log.d(TAG, "Processing " + curProfileFile.getAbsolutePath());
            try {
                if (!curProfileFile.canWrite() && Files.size(curProfileFile.toPath()) == 0) {
                    Log.d(TAG, "Skip profile " + curProfileFile.getAbsolutePath());
                    continue;
                }
                if (curProfileFile.exists() && !curProfileFile.delete()) {
                    try (var writer = new FileOutputStream(curProfileFile)) {
                        Log.d(TAG, "Failed to delete, try to clear content " + curProfileFile.getAbsolutePath());
                    } catch (Throwable e) {
                        Log.e(TAG, "Failed to delete and clear profile file " + curProfileFile.getAbsolutePath(), e);
                    }
                    Os.chmod(curProfileFile.getAbsolutePath(), 00400);
                } else {
                    Files.createFile(curProfileFile.toPath(), attrs);
                }
            } catch (Throwable e) {
                Log.e(TAG, "Failed to disable profile file " + curProfileFile.getAbsolutePath(), e);
            }
        }
    }

    private static void switchAllClassLoader() {
//        var fields = LoadedApk.class.getDeclaredFields();
//        for (Field field : fields) {
//            if (field.getType() == ClassLoader.class) {
//                var obj = XposedHelpers.getObjectField(appLoadedApk, field.getName());
//                XposedHelpers.setObjectField(stubLoadedApk, field.getName(), obj);
//            }
//        }
    }
}
