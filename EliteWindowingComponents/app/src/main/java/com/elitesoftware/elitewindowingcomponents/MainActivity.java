package com.elitesoftware.elitewindowingcomponents;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(32, 32, 32, 32);
        
        TextView title = new TextView(this);
        title.setText("Elite Windowing Components");
        title.setTextSize(24);
        title.setPadding(0, 0, 0, 32);
        layout.addView(title);
        
        TextView desc = new TextView(this);
        desc.setText("This app provides background services and a broadcast API to render native floating windows.");
        desc.setPadding(0, 0, 0, 32);
        layout.addView(desc);
        
        Button btnPerm = new Button(this);
        btnPerm.setText("Grant Overlay Permission");
        btnPerm.setOnClickListener(v -> {
            if (!Settings.canDrawOverlays(this)) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, android.net.Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            } else {
                Toast.makeText(this, "Permission already granted!", Toast.LENGTH_SHORT).show();
            }
        });
        layout.addView(btnPerm);
        
        Button btnTest = new Button(this);
        btnTest.setText("Launch Test Window");
        btnTest.setOnClickListener(v -> {
            Intent intent = new Intent("com.elitesoftware.elitewindowingcomponents.LAUNCH_WINDOW");
            intent.setComponent(new android.content.ComponentName("com.elitesoftware.elitewindowingcomponents", "com.elitesoftware.elitewindowingcomponents.WindowApiReceiver"));
            intent.putExtra("package", getPackageName());
            intent.putExtra("title", "Test Window");
            sendBroadcast(intent);
        });
        layout.addView(btnTest);
        
        setContentView(layout);
    }
}
