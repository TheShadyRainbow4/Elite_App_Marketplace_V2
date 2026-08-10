package com.elitesoftware.elitewindowingcomponents;
import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

public class SettingsLauncherActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent intent = new Intent(this, FloatingWidgetService.class);
        intent.putExtra("package", "com.android.settings");
        intent.putExtra("title", "Elite Settings");
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) { try { intent.putExtra("displayId", getDisplay().getDisplayId()); } catch (Exception e) {} }
        startService(intent);
        finish();
    }
}

