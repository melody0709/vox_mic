package com.voxmic.source;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.media.audiofx.AcousticEchoCanceler;
import android.media.audiofx.AutomaticGainControl;
import android.media.audiofx.NoiseSuppressor;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import androidx.core.content.ContextCompat;

import java.util.Objects;

public class RecordService extends Service {
    private static final String ACTION_RECORD = "com.voxmic.source.RECORD";
    private static final String ACTION_STOP = "com.voxmic.source.STOP";
    private static final String EXTRA_NS = "ns_enabled";
    private static final String EXTRA_AEC = "aec_enabled";
    private static final String EXTRA_AGC = "agc_enabled";

    private static final String CHANNEL_ID = "voxmicsource";
    private static final int NOTIFICATION_ID = 1;

    private static final int SAMPLE_RATE = 48000;
    private static final int CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_MONO;
    private static final int AUDIO_ENCODING = AudioFormat.ENCODING_PCM_16BIT;

    private Thread recorderThread;
    private NoiseSuppressor mNoiseSuppressor;
    private AcousticEchoCanceler mAec;
    private AutomaticGainControl mAgc;

    public static void start(Context context, boolean ns, boolean aec, boolean agc) {
        Intent intent = new Intent(context, RecordService.class)
                .setAction(ACTION_RECORD)
                .putExtra(EXTRA_NS, ns)
                .putExtra(EXTRA_AEC, aec)
                .putExtra(EXTRA_AGC, agc);
        ContextCompat.startForegroundService(context, intent);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        Notification notification = createNotificationStarting();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID,
                    getString(R.string.app_name), NotificationManager.IMPORTANCE_LOW);
            getNotificationManager().createNotificationChannel(channel);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            startForeground(NOTIFICATION_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_NONE);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (Objects.equals(intent.getAction(), ACTION_STOP)) {
            stopSelf();
            return START_NOT_STICKY;
        }

        if (recorderThread != null && recorderThread.isAlive()) {
            return START_NOT_STICKY;
        }

        boolean nsEnabled = intent.getBooleanExtra(EXTRA_NS, true);
        boolean aecEnabled = intent.getBooleanExtra(EXTRA_AEC, true);
        boolean agcEnabled = intent.getBooleanExtra(EXTRA_AGC, true);

        int minBufSize = AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_ENCODING);
        if (minBufSize <= 0) {
            Log.e(App.TAG, "getMinBufferSize failed for 48000Hz, trying 44100Hz");
            minBufSize = AudioRecord.getMinBufferSize(44100, CHANNEL_CONFIG, AUDIO_ENCODING);
        }

        AudioRecord recorder = new AudioRecord(
                MediaRecorder.AudioSource.DEFAULT,
                SAMPLE_RATE,
                CHANNEL_CONFIG,
                AUDIO_ENCODING,
                1 * minBufSize);

        if (recorder.getState() != AudioRecord.STATE_INITIALIZED) {
            Log.e(App.TAG, "Failed to initialize AudioRecord");
            stopSelf();
            return START_NOT_STICKY;
        }

        int sessionId = recorder.getAudioSessionId();
        if (NoiseSuppressor.isAvailable() && nsEnabled) {
            mNoiseSuppressor = NoiseSuppressor.create(sessionId);
            if (mNoiseSuppressor != null) {
                mNoiseSuppressor.setEnabled(true);
                Log.i(App.TAG, "NoiseSuppressor enabled");
            }
        } else {
            Log.i(App.TAG, "NoiseSuppressor " + (nsEnabled ? "not available" : "disabled"));
        }
        if (AcousticEchoCanceler.isAvailable() && aecEnabled) {
            mAec = AcousticEchoCanceler.create(sessionId);
            if (mAec != null) {
                mAec.setEnabled(true);
                Log.i(App.TAG, "AcousticEchoCanceler enabled");
            }
        } else {
            Log.i(App.TAG, "AcousticEchoCanceler " + (aecEnabled ? "not available" : "disabled"));
        }
        if (AutomaticGainControl.isAvailable() && agcEnabled) {
            mAgc = AutomaticGainControl.create(sessionId);
            if (mAgc != null) {
                mAgc.setEnabled(true);
                Log.i(App.TAG, "AutomaticGainControl enabled");
            }
        } else {
            Log.i(App.TAG, "AutomaticGainControl " + (agcEnabled ? "not available" : "disabled"));
        }

        recorderThread = new RecordThread(this, recorder);
        recorderThread.start();
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        if (recorderThread != null) {
            recorderThread.interrupt();
            try { recorderThread.join(); }
            catch (InterruptedException e) { Log.e(App.TAG, "RecordThread.join", e); }
            recorderThread = null;
        }
        if (mNoiseSuppressor != null) { mNoiseSuppressor.release(); mNoiseSuppressor = null; }
        if (mAec != null) { mAec.release(); mAec = null; }
        if (mAgc != null) { mAgc.release(); mAgc = null; }
        stopForeground(true);
    }

    protected void showNotificationListening() {
        Notification notification = new NotificationCompat.Builder(this, CHANNEL_ID)
                .addAction(createStopAction())
                .setContentTitle(getString(R.string.app_name))
                .setContentText(getText(R.string.notification_waiting))
                .setSmallIcon(R.drawable.ic_microphone)
                .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
                .build();
        getNotificationManager().notify(NOTIFICATION_ID, notification);
    }

    protected void showNotificationEstablished() {
        Notification notification = new NotificationCompat.Builder(this, CHANNEL_ID)
                .addAction(createStopAction())
                .setColor(ContextCompat.getColor(this, R.color.ic_launcher_background))
                .setContentTitle(getString(R.string.app_name))
                .setContentText(getText(R.string.notification_forwarding))
                .setSmallIcon(R.drawable.ic_microphone)
                .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
                .build();
        getNotificationManager().notify(NOTIFICATION_ID, notification);
    }

    private NotificationManagerCompat getNotificationManager() {
        return NotificationManagerCompat.from(this);
    }

    private Notification createNotificationStarting() {
        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle(getString(R.string.app_name))
                .setContentText(getText(R.string.notification_starting))
                .setSmallIcon(R.drawable.ic_microphone)
                .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
                .build();
    }

    private Intent createStopIntent() {
        return new Intent(this, RecordService.class).setAction(ACTION_STOP);
    }

    private NotificationCompat.Action createStopAction() {
        Intent stopIntent = createStopIntent();
        PendingIntent stopPendingIntent = PendingIntent.getService(this, 0, stopIntent,
                PendingIntent.FLAG_ONE_SHOT | PendingIntent.FLAG_IMMUTABLE);
        return new NotificationCompat.Action.Builder(0,
                getString(R.string.action_stop), stopPendingIntent).build();
    }
}
