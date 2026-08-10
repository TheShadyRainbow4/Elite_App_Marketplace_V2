package com.elitesoftware.appmarketplace.v2;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import androidx.core.app.NotificationCompat;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class UpdateCheckService extends Service {

    private ScheduledExecutorService scheduler;
    private String serverIp;

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && intent.hasExtra("server_ip")) {
            serverIp = intent.getStringExtra("server_ip");
        }
        if (serverIp == null || serverIp.isEmpty()) {
            serverIp = getSharedPreferences("prefs", MODE_PRIVATE).getString("server_ip", "");
        }

        if (intent != null && "ACTION_INSTALL_UPDATES".equals(intent.getAction())) {
            performBackgroundUpdates();
            return START_STICKY;
        }

        if (scheduler == null || scheduler.isShutdown()) {
            scheduler = Executors.newSingleThreadScheduledExecutor();
            scheduler.scheduleAtFixedRate(this::checkForUpdates, 0, 15, TimeUnit.MINUTES);
        }
        return START_STICKY;
    }

    private void checkForUpdates() {
        if (serverIp == null || serverIp.isEmpty()) return;
        try {
            URL url = new URL("http://" + serverIp + "/api/apps");
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(3000);
            conn.setReadTimeout(5000);
            conn.setRequestMethod("GET");
            BufferedReader in = new BufferedReader(new InputStreamReader(conn.getInputStream()));
            String inputLine;
            StringBuilder response = new StringBuilder();
            while ((inputLine = in.readLine()) != null) response.append(inputLine);
            in.close();

            JSONObject json = new JSONObject(response.toString());
            JSONArray apps = json.getJSONArray("apps");

            int updatesAvailable = 0;
            for (int i = 0; i < apps.length(); i++) {
                JSONObject app = apps.getJSONObject(i);
                if (isUpdateAvailable(app)) {
                    updatesAvailable++;
                }
            }

            if (updatesAvailable > 0) {
                showUpdateNotification(updatesAvailable);
                // Send broadcast to UI to refresh
                Intent intent = new Intent("com.elitesoftware.appmarketplace.v2.UPDATE_AVAILABLE");
                sendBroadcast(intent);
            }

        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private boolean isUpdateAvailable(JSONObject app) {
        try {
            String pkgName = app.optString("package_name");
            android.content.pm.PackageInfo pi = getPackageManager().getPackageInfo(pkgName, 0);
            String installedVersion = pi.versionName;

            JSONArray versions = app.getJSONArray("versions");
            String latestVer = "";
            for (int j = 0; j < versions.length(); j++) {
                String ver = versions.getJSONObject(j).getString("version");
                if (compareVersions(ver, latestVer) > 0) latestVer = ver;
            }

            return compareVersions(latestVer, installedVersion) > 0;
        } catch (Exception e) {
            return false;
        }
    }

    private int compareVersions(String v1, String v2) {
        if (v1 == null) v1 = "";
        if (v2 == null) v2 = "";
        String[] parts1 = v1.replace("v", "").split("\\.");
        String[] parts2 = v2.replace("v", "").split("\\.");
        int length = Math.max(parts1.length, parts2.length);
        for (int i = 0; i < length; i++) {
            int p1 = i < parts1.length && !parts1[i].isEmpty() ? Integer.parseInt(parts1[i].replaceAll("[^0-9]", "0")) : 0;
            int p2 = i < parts2.length && !parts2[i].isEmpty() ? Integer.parseInt(parts2[i].replaceAll("[^0-9]", "0")) : 0;
            if (p1 < p2) return -1;
            if (p1 > p2) return 1;
        }
        return 0;
    }

    private void showUpdateNotification(int updateCount) {
        Intent intent = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        Intent updateIntent = new Intent(this, UpdateCheckService.class);
        updateIntent.setAction("ACTION_INSTALL_UPDATES");
        PendingIntent updatePendingIntent = PendingIntent.getService(this, 1, updateIntent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, "updates")
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentTitle("Updates Available")
                .setContentText(updateCount + " apps have updates available.")
                .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                .setContentIntent(pendingIntent)
                .addAction(R.mipmap.ic_launcher, "Update All", updatePendingIntent)
                .setAutoCancel(true);

        NotificationManager notificationManager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        notificationManager.notify(1001, builder.build());
    }

    private void performBackgroundUpdates() {
        if (serverIp == null || serverIp.isEmpty()) return;
        new Thread(() -> {
            try {
                URL url = new URL("http://" + serverIp + "/api/apps");
                HttpURLConnection conn = (HttpURLConnection) url.openConnection();
                conn.setRequestMethod("GET");
                BufferedReader in = new BufferedReader(new InputStreamReader(conn.getInputStream()));
                String inputLine;
                StringBuilder response = new StringBuilder();
                while ((inputLine = in.readLine()) != null) response.append(inputLine);
                in.close();

                JSONObject json = new JSONObject(response.toString());
                JSONArray apps = json.getJSONArray("apps");
                for (int i = 0; i < apps.length(); i++) {
                    JSONObject app = apps.getJSONObject(i);
                    if (isUpdateAvailable(app)) {
                        installAppInBackground(app);
                    }
                }
                NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
                nm.cancel(1001);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }).start();
    }

    private void installAppInBackground(JSONObject app) {
        try {
            JSONArray versions = app.getJSONArray("versions");
            String latestVer = "";
            String downloadUrl = "";
            for (int j = 0; j < versions.length(); j++) {
                String ver = versions.getJSONObject(j).getString("version");
                if (compareVersions(ver, latestVer) > 0) {
                    latestVer = ver;
                    downloadUrl = versions.getJSONObject(j).getString("url");
                }
            }
            if (downloadUrl.isEmpty()) return;
            if (!downloadUrl.startsWith("http")) downloadUrl = "http://" + serverIp + downloadUrl;

            java.net.URL url = new java.net.URL(downloadUrl);
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.connect();
            int fileLength = conn.getContentLength();
            java.io.InputStream input = new java.io.BufferedInputStream(url.openStream(), 8192);
            java.io.File cacheDir = getCacheDir();
            java.io.File apkFile = new java.io.File(cacheDir, "update_" + app.optString("package_name") + ".apk");
            java.io.OutputStream output = new java.io.FileOutputStream(apkFile);

            byte[] data = new byte[8192];
            int count;
            while ((count = input.read(data)) != -1) {
                output.write(data, 0, count);
            }
            output.flush();
            output.close();
            input.close();

            Process p = null;
            boolean isSu = false;
            try {
                if (rikka.shizuku.Shizuku.pingBinder() && rikka.shizuku.Shizuku.checkSelfPermission() == android.content.pm.PackageManager.PERMISSION_GRANTED) {
                    p = rikka.shizuku.Shizuku.newProcess(new String[]{"pm", "install", "-S", String.valueOf(apkFile.length())}, null, null);
                }
            } catch(Exception e){}
            if (p == null) {
                try {
                    if (com.rosan.dhizuku.api.Dhizuku.isPermissionGranted()) {
                        p = com.rosan.dhizuku.api.Dhizuku.newProcess(new String[]{"pm", "install", "-S", String.valueOf(apkFile.length())}, null, null);
                    }
                } catch(Exception e){}
            }
            if (p == null) {
                isSu = true;
                p = Runtime.getRuntime().exec("su");
                p.getOutputStream().write(("pm install -S " + apkFile.length() + "\n").getBytes());
            }

            if (p != null) {
                java.io.OutputStream out = p.getOutputStream();
                java.io.FileInputStream in2 = new java.io.FileInputStream(apkFile);
                byte[] buf = new byte[65536];
                int len;
                while ((len = in2.read(buf)) > 0) out.write(buf, 0, len);
                out.flush();
                if (isSu) { out.write("\nexit\n".getBytes()); out.flush(); }
                out.close();
                in2.close();
                p.waitFor();
            } else {
                // Fallback to standard installer
                Intent intent = new Intent(Intent.ACTION_VIEW);
                android.net.Uri apkUri = androidx.core.content.FileProvider.getUriForFile(this, getPackageName() + ".provider", apkFile);
                intent.setDataAndType(apkUri, "application/vnd.android.package-archive");
                intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivity(intent);
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            CharSequence name = "App Updates";
            String description = "Notifications for app updates";
            int importance = NotificationManager.IMPORTANCE_DEFAULT;
            NotificationChannel channel = new NotificationChannel("updates", name, importance);
            channel.setDescription(description);
            NotificationManager notificationManager = getSystemService(NotificationManager.class);
            notificationManager.createNotificationChannel(channel);
        }
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        if (scheduler != null) {
            scheduler.shutdownNow();
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
