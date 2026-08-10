package com.elitesoftware.elitewindowingcomponents;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

public class CalculatorLauncherActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent intent = new Intent(this, FloatingWidgetService.class);
        // Try the standard Google Calculator package, which is most common
        intent.putExtra("package", "com.google.android.calculator");
        intent.putExtra("title", "Calculator");
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) { try { intent.putExtra("displayId", getDisplay().getDisplayId()); } catch (Exception e) {} }
        startService(intent);
        finish();
    }
}

