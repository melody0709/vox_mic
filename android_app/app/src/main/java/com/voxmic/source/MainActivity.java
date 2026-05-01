package com.voxmic.source;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.util.ArrayList;

public class MainActivity extends Activity {
    private boolean mNsEnabled = true;
    private boolean mAecEnabled = true;
    private boolean mAgcEnabled = true;

    private static String[] requiredPermissions() {
        ArrayList<String> list = new ArrayList<>();
        list.add(Manifest.permission.RECORD_AUDIO);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            list.add(Manifest.permission.POST_NOTIFICATIONS);
        }
        return list.toArray(new String[0]);
    }

    private String[] missingPermissions() {
        ArrayList<String> list = new ArrayList<>();
        for (String permission : requiredPermissions()) {
            if (ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                list.add(permission);
            }
        }
        return list.toArray(new String[0]);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Intent launchIntent = getIntent();
        mNsEnabled = launchIntent.getBooleanExtra("ns_enabled", true);
        mAecEnabled = launchIntent.getBooleanExtra("aec_enabled", true);
        mAgcEnabled = launchIntent.getBooleanExtra("agc_enabled", true);

        String[] perms = missingPermissions();
        if (perms.length == 0) {
            RecordService.start(this, mNsEnabled, mAecEnabled, mAgcEnabled);
            finish();
        } else {
            ActivityCompat.requestPermissions(this, perms, 0);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        if (missingPermissions().length == 0) {
            RecordService.start(this, mNsEnabled, mAecEnabled, mAgcEnabled);
        }
        finish();
    }
}
